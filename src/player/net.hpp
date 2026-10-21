// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace player {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket invalidSocket = -1;
#endif
uint32_t ticks();
void closeSocket(Socket socket);
void nonblocking(Socket socket);
bool ready(Socket socket, bool write, unsigned milliseconds);
Socket connectSocket(uint32_t ip, unsigned port, const std::atomic<bool> &cancel);
uint32_t resolve(const std::string &host);
void sendAll(Socket socket, const void *data, size_t size, const std::atomic<bool> &cancel);
int receive(Socket socket, void *data, size_t size, const std::atomic<bool> &cancel);
uint32_t read32(const uint8_t *p);
unsigned read16(const uint8_t *p);
void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value);
} // namespace player
