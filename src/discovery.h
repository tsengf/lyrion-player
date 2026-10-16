/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PLAYER_DISCOVERY_H
#define PLAYER_DISCOVERY_H
#include <stdint.h>

typedef void (*player_discovery_log)(const char *message);
/* All IPv4 addresses use network byte order. Zero means no usable result. */
uint32_t player_discovery_broadcast(uint32_t address, uint32_t mask);
uint32_t player_discovery_probe(unsigned port, unsigned timeout_ms, player_discovery_log log);
#endif
