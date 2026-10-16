/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ipifcons.h>
typedef SOCKET discovery_socket;
#define CLOSE_SOCKET closesocket
#define SOCKET_ERROR_CODE() WSAGetLastError()
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
typedef int discovery_socket;
#define INVALID_SOCKET (-1)
#define CLOSE_SOCKET close
#define SOCKET_ERROR_CODE() errno
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "discovery.h"

#define MAX_LAN_TARGETS 32
struct target {
    uint32_t source;
    uint32_t broadcast;
    unsigned index;
    char name[64];
};

uint32_t player_discovery_broadcast(uint32_t address, uint32_t mask)
{
    uint32_t host_bits = ~ntohl(mask);
    /* /0 is not a LAN; /31 and /32 have no broadcast address. */
    if (!address || host_bits < 3 || host_bits == UINT32_MAX ||
        (host_bits & (host_bits + 1)) != 0) return 0;
    return htonl(ntohl(address) | host_bits);
}

static void add_target(struct target targets[], unsigned *count, uint32_t source,
                       uint32_t broadcast, unsigned index, const char *name)
{
    unsigned i;
    if (!source || !broadcast || *count == MAX_LAN_TARGETS) return;
    for (i = 0; i < *count; ++i)
        if (targets[i].source == source && targets[i].index == index) return;
    targets[*count].source = source;
    targets[*count].broadcast = broadcast;
    targets[*count].index = index;
    snprintf(targets[*count].name, sizeof(targets[*count].name), "%s", name);
    ++*count;
}

static unsigned lan_targets(struct target targets[], player_discovery_log log)
{
    unsigned count = 0;
#if PLAYER_NULL_AUDIO
    /* Exercise a distinct, source-bound probe without relying on CI's LAN. */
    const char *test_interface = getenv("PLAYER_TEST_DISCOVERY_INTERFACE");
    if (test_interface) {
        struct in_addr address;
        if (inet_pton(AF_INET, test_interface, &address) == 1)
            add_target(targets, &count, address.s_addr, address.s_addr, 0, "test-lan");
        return count;
    }
#endif
#ifdef _WIN32
    ULONG size = 16384;
    IP_ADAPTER_ADDRESSES *addresses = NULL;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    unsigned attempt;
    for (attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        free(addresses);
        addresses = malloc(size);
        if (!addresses) return 0;
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, addresses, &size);
    }
    if (result == NO_ERROR) {
        IP_ADAPTER_ADDRESSES *adapter;
        for (adapter = addresses; adapter; adapter = adapter->Next) {
            IP_ADAPTER_UNICAST_ADDRESS *unicast;
            if (adapter->OperStatus != IfOperStatusUp || !adapter->IfIndex ||
                adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                adapter->IfType == IF_TYPE_TUNNEL || adapter->IfType == IF_TYPE_PPP) continue;
            for (unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
                struct sockaddr_in *address;
                uint32_t mask;
                unsigned prefix = unicast->OnLinkPrefixLength;
                if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET ||
                    prefix < 1 || prefix > 30 || unicast->DadState != IpDadStatePreferred) continue;
                address = (struct sockaddr_in *)unicast->Address.lpSockaddr;
                mask = htonl(UINT32_MAX << (32 - prefix));
                add_target(targets, &count, address->sin_addr.s_addr,
                    player_discovery_broadcast(address->sin_addr.s_addr, mask),
                    adapter->IfIndex, adapter->AdapterName);
            }
        }
    } else {
        log("Could not enumerate LAN adapters; retaining localhost and general broadcast discovery");
    }
    free(addresses);
#else
    struct ifaddrs *addresses = NULL, *interface;
    if (getifaddrs(&addresses) != 0) {
        log("Could not enumerate LAN interfaces; retaining localhost and general broadcast discovery");
        return 0;
    }
    for (interface = addresses; interface; interface = interface->ifa_next) {
        struct sockaddr_in *address, *mask;
        unsigned required = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
        if (!interface->ifa_addr || !interface->ifa_netmask ||
            interface->ifa_addr->sa_family != AF_INET ||
            (interface->ifa_flags & required) != required ||
            (interface->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT))) continue;
        address = (struct sockaddr_in *)interface->ifa_addr;
        mask = (struct sockaddr_in *)interface->ifa_netmask;
        add_target(targets, &count, address->sin_addr.s_addr,
            player_discovery_broadcast(address->sin_addr.s_addr, mask->sin_addr.s_addr),
            if_nametoindex(interface->ifa_name), interface->ifa_name);
    }
    freeifaddrs(addresses);
#endif
    return count;
}

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

static discovery_socket open_probe(uint32_t source, unsigned index, player_discovery_log log)
{
    struct sockaddr_in local;
    discovery_socket sock = socket(AF_INET, SOCK_DGRAM, 0);
    int enable = 1;
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
#ifndef _WIN32
    if (sock >= FD_SETSIZE) { CLOSE_SOCKET(sock); return INVALID_SOCKET; }
#endif
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&enable, sizeof(enable));
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = source;
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        log("Could not bind a discovery socket; skipping that probe");
        CLOSE_SOCKET(sock);
        return INVALID_SOCKET;
    }
    if (index) {
#ifdef __APPLE__
        if (setsockopt(sock, IPPROTO_IP, IP_BOUND_IF, &index, sizeof(index)) != 0)
            log("Could not pin discovery to the interface; using its bound source address");
#elif defined(_WIN32)
        DWORD interface_index = htonl(index);
        if (setsockopt(sock, IPPROTO_IP, IP_UNICAST_IF, (const char *)&interface_index, sizeof(interface_index)) != 0)
            log("Could not pin discovery to the adapter; using its bound source address");
#endif
    }
    return sock;
}

static void send_probe(discovery_socket sock, uint32_t address, unsigned port,
                       const char *name, player_discovery_log log)
{
    struct sockaddr_in destination;
    char message[192], ip[INET_ADDRSTRLEN];
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons((unsigned short)port);
    destination.sin_addr.s_addr = address;
    inet_ntop(AF_INET, &destination.sin_addr, ip, sizeof(ip));
    snprintf(message, sizeof(message), "Discovery probe via %s to %s:%u", name, ip, port);
    log(message);
    if (sendto(sock, "e", 1, 0, (struct sockaddr *)&destination, sizeof(destination)) < 0) {
        snprintf(message, sizeof(message), "Discovery send via %s failed: %d", name, SOCKET_ERROR_CODE());
        log(message);
    }
}

uint32_t player_discovery_probe(unsigned port, unsigned timeout_ms, player_discovery_log log)
{
    struct target targets[MAX_LAN_TARGETS];
    discovery_socket sockets[MAX_LAN_TARGETS + 1];
    unsigned target_count = lan_targets(targets, log), count = 0, i;
    uint32_t result = 0;
    uint64_t deadline = now_ms() + timeout_ms;
    discovery_socket generic = open_probe(htonl(INADDR_ANY), 0, log);
    if (generic != INVALID_SOCKET) {
        sockets[count++] = generic;
        send_probe(generic, htonl(INADDR_LOOPBACK), port, "localhost", log);
        send_probe(generic, htonl(INADDR_BROADCAST), port, "default route", log);
    }
    for (i = 0; i < target_count; ++i) {
        discovery_socket sock = open_probe(targets[i].source, targets[i].index, log);
        if (sock == INVALID_SOCKET) continue;
        sockets[count++] = sock;
        send_probe(sock, targets[i].broadcast, port, targets[i].name, log);
    }
    if (!count) {
        log("No discovery sockets available; waiting before retry");
#ifdef _WIN32
        Sleep(timeout_ms);
#else
        struct timespec wait = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000 };
        nanosleep(&wait, NULL);
#endif
    }
    while (count) {
        fd_set read_set;
        struct timeval wait;
        int nfds = 0, ready;
        uint64_t now = now_ms(), remaining;
        if (now >= deadline) break;
        remaining = deadline - now;
        FD_ZERO(&read_set);
        for (i = 0; i < count; ++i) {
            FD_SET(sockets[i], &read_set);
#ifndef _WIN32
            if (sockets[i] >= nfds) nfds = sockets[i] + 1;
#endif
        }
        wait.tv_sec = (long)(remaining / 1000);
        wait.tv_usec = (long)((remaining % 1000) * 1000);
        ready = select(nfds, &read_set, NULL, NULL, &wait);
        if (ready <= 0) break;
        for (i = 0; i < count; ++i) {
            struct sockaddr_in sender;
            char reply[256];
#ifdef _WIN32
            int length = sizeof(sender);
#else
            socklen_t length = sizeof(sender);
#endif
            if (!FD_ISSET(sockets[i], &read_set)) continue;
            if (recvfrom(sockets[i], reply, sizeof(reply), 0, (struct sockaddr *)&sender, &length) > 0 &&
                reply[0] == 'E' && sender.sin_port == htons((unsigned short)port)) {
                result = sender.sin_addr.s_addr;
                break;
            }
            log("Ignoring invalid discovery response");
        }
        if (result) break;
    }
    /* Close all sockets, including non-winning interfaces, on every attempt. */
    for (i = 0; i < count; ++i) CLOSE_SOCKET(sockets[i]);
    return result;
}
