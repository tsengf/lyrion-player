// SPDX-License-Identifier: GPL-3.0-or-later
// Wire layout reference: Lyrion's Slim/Networking/Slimproto.pm and
// Slim/Player/Squeezebox.pm (public/9.0). No Squeezelite code is used.
#include "protocol.hpp"
#include "managed.h"
#include "runtime.hpp"
extern "C" {
#include "discovery.h"
}
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace player {
namespace {
void discoveryLog(const char *message) { logMessage(message); }
void waitRetry(unsigned ms) {
    uint32_t start = ticks();
    while (!stopRequested && uint32_t(ticks() - start) < ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
}
std::string addressText(uint32_t ip) {
    in_addr address{};
    address.s_addr = ip;
    char buffer[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &address, buffer, sizeof(buffer));
    return buffer;
}
void packet(Socket s, const std::string &op, const Bytes &body) {
    Bytes frame(8 + body.size());
    std::copy_n(op.begin(), 4, frame.begin());
    put32(frame, 4, uint32_t(body.size()));
    std::copy(body.begin(), body.end(), frame.begin() + 8);
    sendAll(s, frame.data(), frame.size(), stopRequested);
}
void status(Socket s, Playback &p, const std::string &event, uint32_t timestamp = 0) {
    auto snapshot = p.snapshot();
    Bytes body(53);
    std::copy_n(event.begin(), 4, body.begin());
    put32(body, 7, 16384); // Size of the HTTP reader's bounded input buffer.
    put32(body, 15, uint32_t(snapshot.received >> 32));
    put32(body, 19, uint32_t(snapshot.received));
    body[23] = body[24] = 255;
    put32(body, 25, ticks());
    put32(body, 29, uint32_t(Track::capacity * 8 * 4));
    put32(body, 33, snapshot.outputFull);
    put32(body, 37, snapshot.elapsed / 1000);
    put32(body, 43, snapshot.elapsed);
    put32(body, 47, timestamp);
    packet(s, "STAT", body);
}
void hello(Socket s, const Options &o, Playback &p, bool reconnect, const std::string &sync) {
    Bytes body(36);
    body[0] = 12;
    std::copy(o.mac.begin(), o.mac.end(), body.begin() + 2);
    if (reconnect)
        body[24] = 0x40;
    auto received = p.snapshot().received;
    put32(body, 26, uint32_t(received >> 32));
    put32(body, 30, uint32_t(received));
    std::string caps = "Model=lyrion-player,ModelName=Player,Firmware=0.1.0,MaxSampleRate=384000,"
                       "Balance=1,HasPolarityInversion=1,pcm,aif,flc,mp3,ogg";
#ifdef PLAYER_AAC
    caps += ",aac";
#endif
    caps += sync;
    body.insert(body.end(), caps.begin(), caps.end());
    packet(s, "HELO", body);
}
class Session {
    Socket socket;
    Playback &playback;
    uint32_t server;
    std::string &name;

  public:
    uint32_t redirect = 0;
    std::string sync;
    Session(Socket s, Playback &p, uint32_t ip, std::string &n)
        : socket(s), playback(p), server(ip), name(n) {}
    void dispatch(const Bytes &c) {
        if (c.size() < 4)
            throw std::runtime_error("Frame shorter than opcode");
        std::string op(c.begin(), c.begin() + 4);
        auto need = [&](size_t size) {
            if (c.size() < size)
                throw std::runtime_error("Short " + op + " packet");
        };
        if (op == "strm") {
            need(28);
            uint32_t value = read32(c.data() + 18);
            switch (c[4]) {
            case 't':
                status(socket, playback, "STMt", value);
                break;
            case 'q':
                playback.stop();
                status(socket, playback, "STMf");
                break;
            case 'f':
                playback.stop(false);
                status(socket, playback, "STMf");
                break;
            case 'p':
                playback.pause(value);
                if (!value)
                    status(socket, playback, "STMp");
                break;
            case 'a':
                playback.skip(value);
                break;
            case 'u':
                playback.resume(value);
                status(socket, playback, "STMr");
                break;
            case 's':
                status(socket, playback, "STMf");
                try {
                    playback.start(c, server);
                    status(socket, playback, "STMc");
                } catch (const std::exception &e) {
                    logMessage(e.what());
                    status(socket, playback, "STMn");
                }
                break;
            default:
                logMessage("Ignoring unknown stream command");
                break;
            }
        } else if (op == "audg") {
            need(22);
            playback.gain(c[12] ? read32(c.data() + 14) : 65536,
                          c[12] ? read32(c.data() + 18) : 65536);
        } else if (op == "aude") {
            need(6);
            playback.power(c[4] != 0);
        } else if (op == "cont") {
            need(9);
            playback.continuation(read32(c.data() + 4));
        } else if (op == "codc") {
            need(9);
            playback.format({char(c[4]), char(c[5]), char(c[6]), char(c[7]), char(c[8])});
        } else if (op == "setd") {
            need(5);
            if (c[4] == 0) {
                if (c.size() > 5) {
                    auto end = std::find(c.begin() + 5, c.end(), 0);
                    if (end == c.end() || end - (c.begin() + 5) > 64)
                        throw std::runtime_error("Invalid player name");
                    name.assign(c.begin() + 5, end);
                }
                Bytes body{0};
                body.insert(body.end(), name.begin(), name.end());
                body.push_back(0);
                packet(socket, "SETD", body);
            }
        } else if (op == "serv") {
            need(8);
            std::memcpy(&redirect, c.data() + 4, 4);
            if (!redirect || read32(c.data() + 4) == 1)
                throw std::runtime_error("Unsupported server redirect");
            if (c.size() == 18 &&
                std::all_of(c.begin() + 8, c.end(), [](uint8_t b) { return b >= '0' && b <= '9'; }))
                sync = ",SyncgroupID=" + std::string(c.begin() + 8, c.end());
        }
        // Display, IR, firmware and visualization messages do not apply to this player.
    }
    void run() {
        Bytes incoming;
        incoming.reserve(65537);
        uint32_t lastRead = ticks(), lastStatus = ticks();
        while (!stopRequested && !redirect) {
            if (ready(socket, false, 20)) {
                uint8_t buffer[8192];
                int n = int(recv(socket, reinterpret_cast<char *>(buffer), sizeof(buffer), 0));
                if (n <= 0)
                    throw std::runtime_error("Control connection closed");
                lastRead = ticks();
                incoming.insert(incoming.end(), buffer, buffer + n);
                size_t used = 0;
                while (incoming.size() - used >= 2) {
                    unsigned length = read16(incoming.data() + used);
                    if (length < 4)
                        throw std::runtime_error("Invalid SlimProto frame length");
                    if (incoming.size() - used < length + 2)
                        break;
                    Bytes command(incoming.begin() + used + 2,
                                  incoming.begin() + used + 2 + length);
                    dispatch(command);
                    used += length + 2;
                    if (redirect || stopRequested)
                        break;
                }
                incoming.erase(incoming.begin(), incoming.begin() + used);
            }
            for (auto &e : playback.events()) {
                if (e.opcode == "STAT")
                    status(socket, playback, std::string(e.body.begin(), e.body.end()));
                else
                    packet(socket, e.opcode, e.body);
            }
            uint32_t now = ticks();
            if (uint32_t(now - lastStatus) >= 1000) {
                status(socket, playback, "STMt");
                lastStatus = now;
            }
            if (uint32_t(now - lastRead) > 35000)
                throw std::runtime_error("Server heartbeat timed out");
        }
    }
};
} // namespace
void runProtocol(const Options &o, Playback &playback) {
    unsigned port = 3483;
    uint32_t ip = 0;
    bool automatic = o.server.empty(), reconnect = false;
    std::string name = o.name, sync;
#ifdef PLAYER_NULL_AUDIO
    if (const char *value = std::getenv("PLAYER_TEST_SLIMPROTO_PORT")) {
        unsigned long p = std::strtoul(value, nullptr, 10);
        if (p && p <= 65535)
            port = unsigned(p);
    }
#endif
    if (!automatic) {
        auto colon = o.server.find(':');
        std::string host = o.server.substr(0, colon);
        if (colon != std::string::npos)
            port = unsigned(std::stoul(o.server.substr(colon + 1)));
        ip = resolve(host);
    }
    unsigned failures = 0;
    while (!stopRequested) {
        if (!ip) {
            player_managed_connection("discovering", "", port);
            ip = player_discovery_probe(port, 500, discoveryLog);
            if (!ip)
                continue;
        }
        auto host = addressText(ip);
        Socket socket = invalidSocket;
        player_managed_connection("connecting", host.c_str(), port);
        try {
            socket = connectSocket(ip, port, stopRequested);
            player_managed_connection("connected", host.c_str(), port);
            hello(socket, o, playback, reconnect, sync);
            reconnect = true;
            failures = 0;
            Session session(socket, playback, ip, name);
            session.run();
            if (session.redirect) {
                playback.stop();
                ip = session.redirect;
                sync = session.sync;
                reconnect = false;
            }
        } catch (const std::exception &e) {
            if (!stopRequested)
                logMessage(e.what());
            ++failures;
        }
        closeSocket(socket);
        player_managed_connection("disconnected", host.c_str(), port);
        if (automatic && failures >= 3) {
            ip = 0;
            reconnect = false;
        }
        waitRetry(500);
    }
    playback.stop();
}
} // namespace player
