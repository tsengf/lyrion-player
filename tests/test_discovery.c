/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "discovery.h"

static uint32_t ip(const char *value)
{
    struct in_addr address;
    inet_pton(AF_INET, value, &address);
    return address.s_addr;
}

static void log_probe(const char *message) { puts(message); }
static void scan_result(const struct player_discovered_server *server, void *context) {
    (void)context;
    printf("server=%s uuid=%s web=%u\n", server->name, server->uuid, server->web_port);
}

int main(int argc, char **argv)
{
    const char *cases[][3] = {
        { "192.168.1.144", "255.255.255.0", "192.168.1.255" },
        { "10.20.30.40", "255.255.0.0", "10.20.255.255" },
        { "172.16.5.9", "255.255.255.252", "172.16.5.11" },
        { "192.168.1.9", "255.255.255.254", NULL },
        { "192.168.1.9", "255.255.255.255", NULL },
        { "192.168.1.9", "0.0.0.0", NULL },
        { "192.168.1.9", "255.0.255.0", NULL },
        { "0.0.0.0", "255.255.255.0", NULL }
    };
    unsigned i;
    struct player_discovered_server metadata;
    const char message[] = "ENAME\4HomeUUID\3abcJSON\49001";
    if (!player_discovery_parse(message, sizeof(message)-1, &metadata) ||
        strcmp(metadata.name, "Home") || strcmp(metadata.uuid, "abc") || metadata.web_port != 9001 ||
        player_discovery_parse(message, sizeof(message)-2, &metadata) ||
        player_discovery_parse("ENAME", 5, &metadata)) {
        fprintf(stderr, "Incorrect discovery metadata parsing\n"); return 1;
    }
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    if (argc == 3 && !strcmp(argv[1], "--scan")) {
        player_discovery_scan((unsigned)atoi(argv[2]), 1000, scan_result, NULL, log_probe, NULL);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }
    /* Manual UDP-only diagnostic: no HELO, playback, or persistent player. */
    if (argc == 2 && !strcmp(argv[1], "--probe")) {
        struct in_addr found;
        found.s_addr = player_discovery_probe(3483, 3000, log_probe);
        if (found.s_addr) printf("Discovered server: %s\n", inet_ntoa(found));
        else puts("No server replied");
#ifdef _WIN32
        WSACleanup();
#endif
        return found.s_addr ? 0 : 1;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint32_t expected = cases[i][2] ? ip(cases[i][2]) : 0;
        if (player_discovery_broadcast(ip(cases[i][0]), ip(cases[i][1])) != expected) {
            fprintf(stderr, "Incorrect broadcast calculation for %s/%s\n", cases[i][0], cases[i][1]);
            return 1;
        }
    }
#ifdef _WIN32
    WSACleanup();
#endif
    puts("Broadcast subnet checks passed");
    return 0;
}
