/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PLAYER_DISCOVERY_H
#define PLAYER_DISCOVERY_H
#include <stdint.h>
#include <stddef.h>

typedef void (*player_discovery_log)(const char *message);
struct player_discovered_server {
    uint32_t address;
    unsigned port, web_port;
    char name[256], uuid[256];
};
typedef void (*player_discovery_result)(const struct player_discovered_server *, void *);
typedef int (*player_discovery_cancel)(void *);
int player_discovery_parse(const char *packet, size_t length, struct player_discovered_server *server);
void player_discovery_scan(unsigned port, unsigned timeout_ms, player_discovery_result result,
                           void *context, player_discovery_log log, player_discovery_cancel cancel);
/* All IPv4 addresses use network byte order. Zero means no usable result. */
uint32_t player_discovery_broadcast(uint32_t address, uint32_t mask);
uint32_t player_discovery_probe(unsigned port, unsigned timeout_ms, player_discovery_log log);
#endif
