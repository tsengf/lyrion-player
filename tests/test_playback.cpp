// SPDX-License-Identifier: GPL-3.0-or-later
// Test actual HTTP -> PCM -> playback samples without any audio device.
#include "player/playback.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace player;
namespace player {
void logMessage(const std::string &) {}
} // namespace player
static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
class Source {
    Socket listener = invalidSocket;
    std::thread thread;
    std::exception_ptr error;

  public:
    unsigned port = 0;
    Source(unsigned frames, int32_t left, int32_t right) {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        check(listener != invalidSocket, "Create HTTP listener");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(!bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)),
              "Bind HTTP listener");
        check(!listen(listener, 1), "Listen HTTP listener");
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        check(!getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length),
              "HTTP address");
        port = ntohs(address.sin_port);
        thread = std::thread([=] {
            Socket client = invalidSocket;
            try {
                check(ready(listener, false, 5000), "HTTP accept timeout");
                client = accept(listener, nullptr, nullptr);
                check(client != invalidSocket, "Accept HTTP stream");
                nonblocking(client);
                std::atomic<bool> cancel{false};
                char request[1024];
                check(receive(client, request, sizeof(request), cancel) > 0, "HTTP request");
                Bytes data(size_t(frames) * 8);
                for (size_t i = 0; i < frames; ++i) {
                    uint32_t l, r;
                    std::memcpy(&l, &left, 4);
                    std::memcpy(&r, &right, 4);
                    for (unsigned b = 0; b < 4; ++b) {
                        data[i * 8 + b] = uint8_t(l >> (b * 8));
                        data[i * 8 + b + 4] = uint8_t(r >> (b * 8));
                    }
                }
                std::string header =
                    "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(data.size()) +
                    "\r\n\r\n";
                sendAll(client, header.data(), header.size(), cancel);
                sendAll(client, data.data(), data.size(), cancel);
            } catch (...) {
                error = std::current_exception();
            }
            closeSocket(client);
        });
    }
    ~Source() {
        if (thread.joinable())
            thread.join();
        closeSocket(listener);
    }
    void load(Playback &playback, uint32_t gain = 0, unsigned flags = 0, unsigned fade = 0) {
        Bytes command(28);
        std::memcpy(command.data(), "strms1p3421", 11);
        command[13] = 1;
        command[14] = uint8_t('0' + fade);
        command[15] = uint8_t(flags);
        put32(command, 18, gain);
        command[22] = uint8_t(port >> 8);
        command[23] = uint8_t(port);
        const std::string request = "GET / HTTP/1.0\r\n\r\n";
        command.insert(command.end(), request.begin(), request.end());
        playback.start(command, htonl(INADDR_LOOPBACK));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto &event : playback.events()) {
                if (event.opcode == "STAT" && event.body == Bytes{'S', 'T', 'M', 'd'})
                    return;
                check(event.body != Bytes{'S', 'T', 'M', 'n'}, "Decode error");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("Decoder timeout");
    }
};
static void samplesAndControls() {
    Source source(48000, 1000000000, -1000000000);
    Playback p;
    source.load(p, 32768);
    std::vector<int32_t> out(512 * 2);
    p.pause(0);
    p.pull(out.data(), 512, 0);
    check(std::all_of(out.begin(), out.end(), [](auto n) { return n == 0; }),
          "Pause must produce silence");
    check(p.snapshot().outputFull == 48000 * 8, "Pause consumed buffered audio");
    p.resume(0);
    p.power(false);
    p.pull(out.data(), 512, 0);
    check(p.snapshot().outputFull == 48000 * 8, "Power off consumed buffered audio");
    p.power(true);
    p.gain(32768, 131072);
    p.pull(out.data(), 512, 0);
    for (size_t i = 0; i < 512; ++i)
        check(out[i * 2] == 250000000 && out[i * 2 + 1] == -1000000000,
              "Volume/replay gain mismatch");
    check(p.snapshot().elapsed == 10, "Playback clock mismatch");
    p.resume(ticks() + 1000);
    p.pull(out.data(), 512, 0);
    check(std::all_of(out.begin(), out.end(), [](auto n) { return n == 0; }),
          "Scheduled start played early");
    check(p.snapshot().elapsed == 10, "Scheduled wait advanced clock");
    p.resume(0);
    p.skip(10);
    p.pull(out.data(), 512, 0);
    check(p.snapshot().elapsed == 31, "Skip did not advance track clock");
    p.pause(10);
    p.pull(out.data(), 512, 0);
    check(std::all_of(out.begin(), out.begin() + 960, [](auto n) { return n == 0; }),
          "Timed silence missing");
    check(out[960] == 250000000, "Timed silence did not resume");
    p.stop();
    check(p.snapshot().elapsed == 0 && p.snapshot().outputFull == 0, "Stop did not reset");
}
static void queuedTracksAndClipping() {
    Source first(1024, INT32_MAX, INT32_MIN), second(1024, 123456, -654321);
    Playback p;
    first.load(p);
    second.load(p);
    std::vector<int32_t> out(2048 * 2);
    p.gain(131072, 131072);
    p.pull(out.data(), 2048, 0);
    for (size_t i = 0; i < 1024; ++i)
        check(out[i * 2] == INT32_MAX && out[i * 2 + 1] == INT32_MIN, "Gain clipping wrapped");
    for (size_t i = 1024; i < 2048; ++i)
        check(out[i * 2] == 246912 && out[i * 2 + 1] == -1308642,
              "Track boundary lost or inserted frames");
    check(p.snapshot().elapsed == 21, "Track boundary did not reset clock");
    unsigned starts = 0;
    for (const auto &e : p.events())
        if (e.body == Bytes{'S', 'T', 'M', 's'})
            ++starts;
    check(starts == 2, "Track start events missing");
    p.pull(out.data(), 512, 0);
    auto events = p.events();
    check(std::any_of(events.begin(), events.end(),
                      [](auto &e) { return e.body == Bytes{'S', 'T', 'M', 'u'}; }),
          "End event missing");
}
static void polarityAndMono() {
    Source source(1024, 123456, -654321);
    Playback p;
    source.load(p, 0, 3 | 4);
    int32_t out[2];
    p.pull(out, 1, 0);
    check(out[0] == -123456 && out[1] == -123456, "Polarity or mono channel selection failed");
}
static void fadesAndCrossfade() {
    {
        Source source(48000, 1000000000, 1000000000);
        Playback p;
        source.load(p, 0, 0, 2);
        std::vector<int32_t> out(48000 * 2);
        p.pull(out.data(), 48000, 0);
        check(out[0] == 0 && out[24000 * 2] == 500000000 && out.back() > 999000000,
              "Fade-in envelope failed");
    }
    {
        Source source(48000, 1000000000, 1000000000);
        Playback p;
        source.load(p, 0, 0, 3);
        std::vector<int32_t> out(48000 * 2);
        p.pull(out.data(), 48000, 0);
        check(out[0] == 1000000000 && out[24000 * 2] == 500000000 && out.back() < 30000,
              "Fade-out envelope failed");
    }
    {
        Source first(48000, 1000000000, 1000000000), second(48000, -1000000000, -1000000000);
        Playback p;
        first.load(p);
        second.load(p, 0, 0, 1);
        std::vector<int32_t> out(48000 * 2);
        p.pull(out.data(), 48000, 0);
        check(out[0] == 1000000000 && out[24000 * 2] == 0 && out.back() < -999000000,
              "Crossfade mixing failed");
        check(p.snapshot().outputFull == 0, "Crossfade did not consume both tracks");
    }
}
int main() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
#endif
    try {
        samplesAndControls();
        queuedTracksAndClipping();
        polarityAndMono();
        fadesAndCrossfade();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout
        << "HTTP decoding, sample values, clipping, timing and queued track boundaries passed\n";
}
