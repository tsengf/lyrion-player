// SPDX-License-Identifier: GPL-3.0-or-later
#include "managed.h"
#include "native_audio.h"
#include "protocol.hpp"
#include "runtime.hpp"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpg123.h>
#include <mutex>
#include <random>
#include <regex>
#include <stdexcept>
namespace player {
std::atomic<bool> stopRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "Signal handler requires lock-free atomic");
static std::mutex logMutex;
static Playback *audioPlayback = nullptr;
void logMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::fprintf(stderr, "%s\n", message.c_str());
}
static void audioPull(void *out, unsigned frames, unsigned latency) {
    audioPlayback->pull(out, frames, latency);
}
static void audioStatus(const char *message, int available) {
    logMessage(message);
    player_managed_audio(available);
}
static void stopSignal(int) { stopRequested.store(true); }
static unsigned port(const std::string &text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid port");
    auto n = std::stoul(text);
    if (!n || n > 65535)
        throw std::runtime_error("Invalid port");
    return unsigned(n);
}
} // namespace player
int main(int argc, char **argv) {
    using namespace player;
    Options options;
    bool managed = false;
    std::string logfile;
    try {
        std::string mac;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for " + arg);
                return argv[i];
            };
            if (arg == "--help" || arg == "-?" || arg == "-h") {
                std::puts("Player 0.1.0 - independent Lyrion Music Server player\n"
                          "--server, -s HOST[:PORT]  Server (otherwise discover automatically)\n"
                          "--name, -n NAME          Player name\n--mac, -m MAC            Saved "
                          "player identity\n"
                          "--managed               JSON status and stop commands on stdin\n"
                          "--logfile, -f FILE      Append diagnostics\n--debug, -d LOG=LEVEL    "
                          "Accepted for tray compatibility\n"
                          "--list-devices, -l       Describe the system default output");
                return 0;
            } else if (arg == "--list-devices" || arg == "-l") {
                std::puts("default - System default output (automatically follows changes)");
                return 0;
            } else if (arg == "--managed")
                managed = true;
            else if (arg == "--server" || arg == "-s")
                options.server = value();
            else if (arg == "--name" || arg == "-n")
                options.name = value();
            else if (arg == "--mac" || arg == "-m")
                mac = value();
            else if (arg == "--logfile" || arg == "-f") {
                logfile = value();
                if (!logfile.empty()) {
#ifdef _WIN32
                    int n = MultiByteToWideChar(CP_UTF8, 0, logfile.c_str(), -1, nullptr, 0);
                    std::wstring wide(size_t(n), L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, logfile.c_str(), -1, wide.data(), n);
                    if (!_wfreopen(wide.c_str(), L"a", stderr))
                        throw std::runtime_error("Cannot open log file");
#else
                    if (!std::freopen(logfile.c_str(), "a", stderr))
                        throw std::runtime_error("Cannot open log file");
#endif
                }
            } else if (arg == "--debug" || arg == "-d")
                (void)value();
            else if (arg == "--output" || arg == "-o" || arg == "--audio-params" || arg == "-a" ||
                     arg == "--rates" || arg == "-r") {
                auto device = value();
                if ((arg == "--output" || arg == "-o") && device == "-" &&
                    std::any_of(argv + 1, argv + argc,
                                [](const char *v) { return !std::strcmp(v, "--managed"); }))
                    throw std::runtime_error("--managed cannot use stdout audio");
                if ((arg != "--output" && arg != "-o") || device != "default")
                    throw std::runtime_error(
                        "Player requires default output and manages its own format");
            } else
                throw std::runtime_error("Unknown option " + arg);
        }
        if (options.name.empty() || options.name.size() > 64 ||
            options.name.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Invalid player name");
        if (!options.server.empty()) {
            auto colon = options.server.find(':');
            if (colon == 0 || options.server.find_first_of("/\\ \r\n\t") != std::string::npos)
                throw std::runtime_error("Invalid server");
            if (colon != std::string::npos)
                port(options.server.substr(colon + 1));
        }
        if (!mac.empty()) {
            if (!std::regex_match(mac, std::regex("([a-fA-F0-9]{2}:){5}[a-fA-F0-9]{2}")))
                throw std::runtime_error("Invalid player MAC");
            for (size_t i = 0; i < 6; ++i)
                options.mac[i] = uint8_t(std::stoul(mac.substr(i * 3, 2), nullptr, 16));
            if (options.mac[0] & 1 || std::all_of(options.mac.begin(), options.mac.end(),
                                                  [](uint8_t b) { return b == 0; }))
                throw std::runtime_error("Player MAC must be nonzero and unicast");
        } else {
            // Tray players always supply a persisted identity. Standalone runs use an ephemeral
            // one.
            std::random_device random;
            options.mac[0] = 2;
            for (size_t i = 1; i < 6; ++i)
                options.mac[i] = uint8_t(random());
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Option error: %s\n", e.what());
        return 1;
    }
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::signal(SIGINT, stopSignal);
    std::signal(SIGTERM, stopSignal);
    if (managed)
        player_managed_enable();
    player_managed_start();
    if (mpg123_init() != MPG123_OK) {
        logMessage("Cannot initialize MP3 library");
        return 1;
    }
    int result = 0;
    {
        Playback playback;
        audioPlayback = &playback;
        if (!player_audio_start(audioPull, audioStatus)) {
            logMessage("Cannot start native audio");
            result = 1;
        } else {
            try {
                runProtocol(options, playback);
            } catch (const std::exception &e) {
                logMessage(e.what());
                result = 1;
            }
            // Stop producers, then join callbacks before releasing playback buffers.
            playback.stop();
            player_audio_stop();
        }
    }
    mpg123_exit();
    player_managed_connection("stopped", "", 0);
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}
