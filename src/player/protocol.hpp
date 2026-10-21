// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "playback.hpp"
namespace player {
struct Options {
    std::string server, name = "Lyrion Player";
    std::array<uint8_t, 6> mac{};
};
// Runs until stopRequested. Native audio lifetime is owned by main.
void runProtocol(const Options &options, Playback &playback);
} // namespace player
