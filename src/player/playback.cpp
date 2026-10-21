// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace player {
Playback::~Playback() { stop(); }
void Playback::emit(const std::string &op, Bytes body) {
    std::lock_guard<std::mutex> lock(mutex);
    // A peer that stops reading must not cause unbounded event accumulation.
    if (pending.size() < 128)
        pending.push_back({op, std::move(body)});
}
void Playback::statusLocked(const char *code) {
    if (pending.size() < 128)
        pending.push_back({"STAT", Bytes(code, code + 4)});
}
void Playback::log(const std::string &message) { logMessage(message); }
std::vector<Event> Playback::events() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Event> result;
    result.swap(pending);
    return result;
}
void Playback::stop(bool flush) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (downloading)
            downloading->cancel = true;
    }
    changed.notify_all();
    if (worker.joinable())
        worker.join();
    std::lock_guard<std::mutex> lock(mutex);
    if (flush) {
        tracks.clear();
        current.reset();
        running = false;
        buffering = false;
        pauseFrames = skipFrames = 0;
        startAt = 0;
        pending.clear();
    } else {
        // Flush a prefetched track, retaining audio already playing.
        tracks.erase(
            std::remove_if(tracks.begin(), tracks.end(), [](auto &t) { return !t->started; }),
            tracks.end());
        if (downloading)
            downloading->done = true;
    }
    downloading.reset();
}
void Playback::start(const Bytes &c, uint32_t server) {
    if (c.size() < 28)
        throw std::runtime_error("Short strm start");
    if (c[5] < '0' || c[5] > '3')
        throw std::runtime_error("Invalid autostart mode");
    if (c[15] & 0x20)
        throw std::runtime_error("HTTPS requires server proxying");
    if (!read16(c.data() + 22) || c.size() == 28)
        throw std::runtime_error("Missing stream address or HTTP request");
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (downloading)
            downloading->cancel = true;
    }
    changed.notify_all();
    if (worker.joinable())
        worker.join();
    auto t = std::make_shared<Track>();
    t->autostart = c[5] - '0';
    t->continued = t->autostart < 2;
    t->format = {char(c[6]), char(c[7]), char(c[8]), char(c[9]), char(c[10])};
    t->formatKnown = c[6] != '?';
    t->streamThreshold = unsigned(c[11]) * 1024;
    t->outputThreshold = std::min(unsigned(c[16]) * 4800, unsigned(Track::capacity));
    t->fadeType = c[14] >= '0' && c[14] <= '4' ? unsigned(c[14] - '0') : 0;
    t->fadeFrames = uint64_t(c[13]) * 48000;
    t->replayGain = read32(c.data() + 18);
    t->invert = (c[15] & 3) == 3;
    t->channels = (c[15] >> 2) & 3;
    uint32_t ip;
    std::memcpy(&ip, c.data() + 24, 4);
    if (!ip)
        ip = server;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (tracks.size() >= 4)
            throw std::runtime_error("Too many queued tracks");
        if (downloading)
            downloading->done = true;
        downloading = t;
        tracks.push_back(t);
        if (t->autostart % 2 && !running) {
            running = true;
            buffering = true;
        }
    }
    std::string request(c.begin() + 28, c.end());
    worker =
        std::thread(&Playback::download, this, t, ip, read16(c.data() + 22), std::move(request));
}
void Playback::download(const std::shared_ptr<Track> &t, uint32_t ip, unsigned port,
                        std::string request) {
    bool failed = false;
    try {
        HttpStream stream(*this, t, ip, port, request);
        Format f;
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return t->cancel || (t->continued && t->formatKnown); });
            if (t->cancel)
                return;
            f = t->format;
        }
        decode(stream, f, *this, t);
        stream.complete();
    } catch (const std::exception &e) {
        if (!t->cancel) {
            log(std::string("Stream failed: ") + e.what());
            if (!t->networkEnded) {
                t->networkEnded = true;
                emit("DSCO", Bytes{2});
            }
            failed = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Publish completion before the callback can observe an empty, finished track.
        if (!t->cancel) {
            if (!t->readySent && t->autostart % 2 == 0)
                statusLocked("STMl");
            statusLocked(failed ? "STMn" : "STMd");
        }
        t->done = true;
    }
    changed.notify_all();
}
void Playback::write(const std::shared_ptr<Track> &t, const int32_t *data, size_t frames) {
    while (frames) {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return t->cancel || t->count < Track::capacity; });
        if (t->cancel)
            throw std::runtime_error("Cancelled");
        size_t tail = (t->head + t->count) % Track::capacity;
        size_t n = std::min({frames, Track::capacity - t->count, Track::capacity - tail});
        std::copy_n(data, n * 2, t->samples.data() + tail * 2);
        t->count += n;
        t->produced += n;
        data += n * 2;
        frames -= n;
        if (!t->readySent && t->autostart % 2 == 0 &&
            (t->received >= t->streamThreshold || t->count == Track::capacity)) {
            t->readySent = true;
            statusLocked("STMl");
        }
    }
}
void Playback::pause(unsigned ms) {
    std::lock_guard<std::mutex> lock(mutex);
    if (ms)
        pauseFrames = uint64_t(ms) * 48;
    else {
        running = false;
        pauseFrames = 0;
    }
}
void Playback::resume(uint32_t at) {
    std::lock_guard<std::mutex> lock(mutex);
    startAt = at;
    running = true;
    buffering = false;
}
void Playback::skip(unsigned ms) {
    std::lock_guard<std::mutex> lock(mutex);
    skipFrames = uint64_t(ms) * 48;
}
void Playback::power(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    powered = enabled;
}
void Playback::gain(uint32_t left, uint32_t right) {
    std::lock_guard<std::mutex> lock(mutex);
    leftGain = left;
    rightGain = right;
}
void Playback::continuation(unsigned interval) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (downloading && !downloading->continued) {
            downloading->metaInterval = interval;
            downloading->continued = true;
        }
    }
    changed.notify_all();
}
void Playback::format(const Format &f) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (downloading) {
            downloading->format = f;
            downloading->formatKnown = true;
        }
    }
    changed.notify_all();
}
Snapshot Playback::snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    Snapshot s;
    if (downloading)
        s.received = downloading->received;
    for (const auto &t : tracks)
        s.outputFull += uint32_t(t->count * 8);
    if (current)
        s.elapsed =
            uint32_t((current->played > deviceLatency ? current->played - deviceLatency : 0) / 48);
    s.playing = running && powered;
    return s;
}
void Playback::pull(void *out, unsigned frames, unsigned latency) {
    auto samples = static_cast<int32_t *>(out);
    std::fill_n(samples, size_t(frames) * 2, 0);
    std::lock_guard<std::mutex> lock(mutex);
    deviceLatency = latency;
    if (!running || !powered)
        return;
    if (startAt && int32_t(ticks() - startAt) < 0)
        return;
    startAt = 0;
    if (pauseFrames) {
        unsigned n = unsigned(std::min<uint64_t>(frames, pauseFrames));
        pauseFrames -= n;
        frames -= n;
        samples += n * 2;
        if (!frames)
            return;
    }
    while (frames && !tracks.empty()) {
        auto &t = tracks.front();
        if (buffering) {
            if (t->count < t->outputThreshold && !t->done && t->count < Track::capacity)
                break;
            buffering = false;
        }
        if (!t->count) {
            if (t->done) {
                tracks.pop_front();
                if (tracks.empty()) {
                    running = false;
                    statusLocked("STMu");
                }
                continue;
            }
            if (t->started && !t->underrun) {
                t->underrun = true;
                statusLocked("STMo");
            }
            break;
        }
        if (!t->started) {
            t->started = true;
            current = t;
            statusLocked("STMs");
        }
        t->underrun = false;
        size_t n = std::min({size_t(frames), t->count, Track::capacity - t->head});
        if (skipFrames) {
            n = std::min<uint64_t>(n, skipFrames);
            skipFrames -= n;
        } else {
            auto sample = [&](const Track &track, size_t index, unsigned ch) {
                unsigned channel = ch;
                if (track.channels == 1)
                    channel = 0;
                if (track.channels == 2)
                    channel = 1;
                double value =
                    track.samples[((track.head + index) % Track::capacity) * 2 + channel];
                uint32_t rg = track.replayGain ? track.replayGain : 65536;
                value *= double(ch ? rightGain : leftGain) / 65536.0 * rg / 65536.0;
                if (track.invert)
                    value = -value;
                if (track.fadeFrames) {
                    if (track.fadeType == 2 || track.fadeType == 4)
                        value *=
                            std::min(1.0, double(track.played + index) / double(track.fadeFrames));
                    if ((track.fadeType == 3 || track.fadeType == 4) && track.done)
                        value *=
                            std::min(1.0, double(track.count - index) / double(track.fadeFrames));
                }
                return value;
            };
            std::shared_ptr<Track> next = tracks.size() > 1 ? tracks[1] : nullptr;
            bool crossfade = false;
            if (next && t->done && next->fadeType == 1 && next->fadeFrames) {
                if (t->count > next->fadeFrames) {
                    n = std::min<uint64_t>(n, t->count - next->fadeFrames);
                } else {
                    if (!next->crossfadeFrames && next->count >= t->count) {
                        next->crossfadeFrames = t->count;
                        next->started = true;
                        current = next;
                        statusLocked("STMs");
                    }
                    crossfade = next->crossfadeFrames && next->count;
                    if (crossfade)
                        n = std::min(n, next->count);
                }
            }
            for (size_t i = 0; i < n; ++i)
                for (unsigned ch = 0; ch < 2; ++ch) {
                    double value = sample(*t, i, ch);
                    if (crossfade) {
                        double fraction =
                            double(next->crossfadeDone + i) / double(next->crossfadeFrames);
                        value = value * (1.0 - fraction) + sample(*next, i, ch) * fraction;
                    }
                    samples[i * 2 + ch] =
                        int32_t(std::clamp(value, double(INT32_MIN), double(INT32_MAX)));
                }
            if (crossfade) {
                next->head = (next->head + n) % Track::capacity;
                next->count -= n;
                next->played += n;
                next->crossfadeDone += n;
            }
            samples += n * 2;
            frames -= unsigned(n);
        }
        t->head = (t->head + n) % Track::capacity;
        t->count -= n;
        t->played += n;
    }
    changed.notify_all();
}
} // namespace player
