// SPDX-License-Identifier: GPL-3.0-or-later
#include "net.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#endif
namespace player {
uint32_t ticks() {
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}
void closeSocket(Socket s) {
    if (s == invalidSocket)
        return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}
void nonblocking(Socket s) {
#ifdef _WIN32
    u_long one = 1;
    if (ioctlsocket(s, FIONBIO, &one))
        throw std::runtime_error("Cannot set nonblocking socket");
#else
    if (fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK) < 0)
        throw std::runtime_error("Cannot set nonblocking socket");
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
}
bool ready(Socket s, bool write, unsigned ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval timeout{};
    timeout.tv_sec = ms / 1000;
    timeout.tv_usec = (ms % 1000) * 1000;
    int result =
        select(int(s + 1), write ? nullptr : &set, write ? &set : nullptr, nullptr, &timeout);
    if (result < 0) {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEINTR)
            return false;
#else
        if (errno == EINTR)
            return false;
#endif
        throw std::runtime_error("Socket select failed");
    }
    return result > 0;
}
static bool transient() {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == EINPROGRESS;
#endif
}
Socket connectSocket(uint32_t ip, unsigned port, const std::atomic<bool> &cancel) {
    Socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == invalidSocket)
        throw std::runtime_error("Cannot create socket");
    try {
        nonblocking(s);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ip;
        address.sin_port = htons(uint16_t(port));
        if (connect(s, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            if (!transient())
                throw std::runtime_error("Connection refused");
            uint32_t start = ticks();
            while (!cancel && uint32_t(ticks() - start) < 5000) {
                if (!ready(s, true, 50))
                    continue;
                int error = 0;
#ifdef _WIN32
                int length = sizeof(error);
#else
                socklen_t length = sizeof(error);
#endif
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                               &length) ||
                    error)
                    throw std::runtime_error("Connection failed");
                return s;
            }
            throw std::runtime_error(cancel ? "Cancelled" : "Connection timed out");
        }
        return s;
    } catch (...) {
        closeSocket(s);
        throw;
    }
}
uint32_t resolve(const std::string &host) {
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &addresses) || !addresses)
        throw std::runtime_error("Cannot resolve server hostname");
    auto ip = reinterpret_cast<sockaddr_in *>(addresses->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(addresses);
    return ip;
}
void sendAll(Socket s, const void *data, size_t size, const std::atomic<bool> &cancel) {
    const char *p = static_cast<const char *>(data);
    uint32_t last = ticks();
    while (size) {
        if (cancel)
            throw std::runtime_error("Cancelled");
        if (uint32_t(ticks() - last) > 5000)
            throw std::runtime_error("Socket write timed out");
        if (!ready(s, true, 50))
            continue;
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        int n = int(send(s, p, int(size), flags));
        if (n < 0 && transient())
            continue;
        if (n <= 0)
            throw std::runtime_error("Socket write failed");
        p += n;
        size -= size_t(n);
        last = ticks();
    }
}
int receive(Socket s, void *data, size_t size, const std::atomic<bool> &cancel) {
    uint32_t last = ticks();
    while (!cancel) {
        if (uint32_t(ticks() - last) > 15000)
            throw std::runtime_error("Stream read timed out");
        if (!ready(s, false, 50))
            continue;
        int n = int(recv(s, static_cast<char *>(data), int(size), 0));
        if (n < 0 && transient())
            continue;
        if (n < 0)
            throw std::runtime_error("Socket read failed");
        return n;
    }
    throw std::runtime_error("Cancelled");
}
uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
unsigned read16(const uint8_t *p) { return unsigned(p[0]) << 8 | p[1]; }
void put32(std::vector<uint8_t> &v, size_t o, uint32_t n) {
    v[o] = uint8_t(n >> 24);
    v[o + 1] = uint8_t(n >> 16);
    v[o + 2] = uint8_t(n >> 8);
    v[o + 3] = uint8_t(n);
}
} // namespace player
