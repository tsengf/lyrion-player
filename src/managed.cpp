// SPDX-License-Identifier: GPL-3.0-or-later
#include "managed.h"
#include "player/runtime.hpp"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
namespace {
bool enabled = false;
std::mutex events;
} // namespace
void player_managed_enable() { enabled = true; }
int player_managed_enabled() { return enabled; }
int player_managed_stopping() { return player::stopRequested.load(); }
void player_managed_start() {
    if (!enabled)
        return;
    std::thread([] {
        char command[128];
        while (std::fgets(command, sizeof(command), stdin)) {
            if (!std::strcmp(command, "stop\n") || !std::strcmp(command, "stop\r\n"))
                break;
        }
        player::stopRequested = true;
    }).detach();
}
void player_managed_connection(const char *state, const char *host, unsigned port) {
    if (!enabled)
        return;
    std::lock_guard<std::mutex> lock(events);
    std::printf(
        "{\"version\":1,\"type\":\"connection\",\"state\":\"%s\",\"host\":\"%s\",\"port\":%u}\n",
        state, host ? host : "", port);
    std::fflush(stdout);
}
void player_managed_audio(int available) {
    if (!enabled || available < 0)
        return;
    std::lock_guard<std::mutex> lock(events);
    std::printf("{\"version\":1,\"type\":\"audio\",\"state\":\"%s\"}\n",
                available ? "ready" : "unavailable");
    std::fflush(stdout);
}
