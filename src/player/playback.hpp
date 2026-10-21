// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "net.hpp"
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
namespace player {
using Bytes = std::vector<uint8_t>;
struct Event {
    std::string opcode;
    Bytes body;
};
struct Format {
    char codec = 'p', size = '1', rate = '3', channels = '2', endian = '1';
};
struct Track {
    static constexpr size_t capacity = 48000 * 8;
    std::vector<int32_t> samples = std::vector<int32_t>(capacity * 2);
    size_t head = 0, count = 0;
    uint64_t played = 0, produced = 0;
    std::atomic<uint64_t> received{0};
    std::atomic<bool> cancel{false};
    bool done = false, started = false, underrun = false, continued = true, formatKnown = true;
    bool readySent = false;
    bool networkEnded = false; // Only the decoder worker accesses this flag.
    std::atomic<unsigned> metaInterval{0};
    unsigned outputThreshold = 0, streamThreshold = 0;
    uint32_t replayGain = 0;
    unsigned fadeType = 0;
    uint64_t fadeFrames = 0, crossfadeFrames = 0, crossfadeDone = 0;
    unsigned channels = 0;
    bool invert = false;
    int autostart = 1;
    Format format;
};
struct Snapshot {
    uint64_t received = 0;
    uint32_t elapsed = 0, outputFull = 0;
    bool playing = false;
};
class Playback {
  public:
    Playback() = default;
    ~Playback();
    void start(const Bytes &command, uint32_t server);
    void stop(bool flush = true);
    void pause(unsigned ms);
    void resume(uint32_t at);
    void skip(unsigned ms);
    void power(bool enabled);
    void gain(uint32_t left, uint32_t right);
    void continuation(unsigned interval);
    void format(const Format &format);
    void pull(void *samples, unsigned frames, unsigned latency);
    Snapshot snapshot();
    std::vector<Event> events();
    void emit(const std::string &opcode, Bytes body);
    void write(const std::shared_ptr<Track> &track, const int32_t *samples, size_t frames);
    void log(const std::string &message);

  private:
    friend class HttpStream;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Track>> tracks;
    std::shared_ptr<Track> downloading, current;
    std::thread worker;
    std::vector<Event> pending;
    bool running = false, powered = true, buffering = false;
    uint32_t startAt = 0, leftGain = 65536, rightGain = 65536;
    uint64_t pauseFrames = 0, skipFrames = 0;
    unsigned deviceLatency = 0;
    void statusLocked(const char *code);
    void download(const std::shared_ptr<Track> &track, uint32_t ip, unsigned port,
                  std::string request);
};
// Blocking, cancellable HTTP body reader. Handles chunked transport and ICY metadata.
class HttpStream {
  public:
    HttpStream(Playback &owner, std::shared_ptr<Track> track, uint32_t ip, unsigned port,
               const std::string &request);
    ~HttpStream();
    size_t read(void *out, size_t size);
    void exact(void *out, size_t size);
    void discard(uint64_t size);
    void complete();

  private:
    Playback &owner;
    std::shared_ptr<Track> track;
    Socket socket = invalidSocket;
    std::array<uint8_t, 16384> buffer{};
    size_t pos = 0, available = 0;
    bool chunked = false, ended = false, hasLength = false, disconnectSent = false;
    uint64_t remaining = 0, chunkRemaining = 0;
    unsigned metaRemaining = 0;
    int byte();
    std::string line();
    size_t raw(void *out, size_t size);
    size_t body(void *out, size_t size);
};
void decode(HttpStream &stream, const Format &format, Playback &owner,
            const std::shared_ptr<Track> &track);
void logMessage(const std::string &message);
} // namespace player
