// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
namespace player {
static std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
HttpStream::HttpStream(Playback &o, std::shared_ptr<Track> t, uint32_t ip, unsigned port,
                       const std::string &request)
    : owner(o), track(std::move(t)) {
    try {
        socket = connectSocket(ip, port, track->cancel);
        sendAll(socket, request.data(), request.size(), track->cancel);
        std::string headers, first = line();
        headers = first + "\r\n";
        auto space = first.find(' ');
        if (space == std::string::npos || first.substr(space + 1, 3) != "200")
            throw std::runtime_error("HTTP response is not 200; server must handle redirects");
        for (;;) {
            auto h = line();
            headers += h + "\r\n";
            if (h.empty())
                break;
            if (headers.size() > 32768)
                throw std::runtime_error("HTTP headers too large");
            auto colon = h.find(':');
            if (colon == std::string::npos)
                continue;
            auto key = lower(h.substr(0, colon)), value = lower(h.substr(colon + 1));
            if (key == "transfer-encoding") {
                if (value.find("chunked") == std::string::npos)
                    throw std::runtime_error("Unsupported transfer encoding");
                chunked = true;
            }
            if (key == "content-length") {
                size_t end = 0;
                remaining = std::stoull(value, &end);
                if (value.find_first_not_of(" \t", end) != std::string::npos)
                    throw std::runtime_error("Invalid Content-Length");
                hasLength = true;
            }
            if (key == "content-encoding" && value.find("identity") == std::string::npos)
                throw std::runtime_error("Compressed HTTP transport unsupported");
        }
        owner.emit("RESP", Bytes(headers.begin(), headers.end()));
        {
            std::unique_lock<std::mutex> lock(owner.mutex);
            owner.changed.wait(lock, [&] { return track->cancel || track->continued; });
            metaRemaining = track->metaInterval;
        }
    } catch (...) {
        closeSocket(socket);
        socket = invalidSocket;
        throw;
    }
}
HttpStream::~HttpStream() { closeSocket(socket); }
int HttpStream::byte() {
    if (pos == available) {
        int n = receive(socket, buffer.data(), buffer.size(), track->cancel);
        if (!n)
            return -1;
        available = size_t(n);
        pos = 0;
    }
    return buffer[pos++];
}
std::string HttpStream::line() {
    std::string result;
    for (;;) {
        int c = byte();
        if (c < 0)
            throw std::runtime_error("Truncated HTTP line");
        if (c == '\n') {
            if (!result.empty() && result.back() == '\r')
                result.pop_back();
            return result;
        }
        if (result.size() >= 8192)
            throw std::runtime_error("HTTP line too large");
        result.push_back(char(c));
    }
}
size_t HttpStream::raw(void *out, size_t size) {
    if (!size)
        return 0;
    if (pos < available) {
        size_t n = std::min(size, available - pos);
        std::memcpy(out, buffer.data() + pos, n);
        pos += n;
        return n;
    }
    return size_t(receive(socket, out, std::min(size, size_t(16384)), track->cancel));
}
size_t HttpStream::body(void *out, size_t size) {
    if (ended || !size)
        return 0;
    if (chunked) {
        while (!chunkRemaining) {
            auto h = line();
            size_t end = 0;
            chunkRemaining = std::stoull(h, &end, 16);
            if (end < h.size() && h[end] != ';' && h[end] != ' ')
                throw std::runtime_error("Invalid HTTP chunk size");
            if (!chunkRemaining) {
                size_t trailers = 0;
                for (;;) {
                    auto trailer = line();
                    trailers += trailer.size();
                    if (trailers > 32768)
                        throw std::runtime_error("HTTP trailers too large");
                    if (trailer.empty())
                        break;
                }
                ended = true;
                return 0;
            }
        }
        size = std::min<uint64_t>(size, chunkRemaining);
    } else if (hasLength) {
        if (!remaining) {
            ended = true;
            return 0;
        }
        size = std::min<uint64_t>(size, remaining);
    }
    size_t n = raw(out, size);
    if (!n) {
        if (chunked || (hasLength && remaining))
            throw std::runtime_error("Truncated HTTP body");
        ended = true;
        return 0;
    }
    if (chunked) {
        chunkRemaining -= n;
        if (!chunkRemaining && (byte() != '\r' || byte() != '\n'))
            throw std::runtime_error("Invalid chunk terminator");
    } else if (hasLength)
        remaining -= n;
    return n;
}
size_t HttpStream::read(void *out, size_t size) {
    if (!size)
        return 0;
    unsigned interval = track->metaInterval; // Set once, before continuation releases the worker.
    if (interval && !metaRemaining && !ended) {
        uint8_t length = 0;
        if (!body(&length, 1))
            goto eof;
        Bytes metadata(size_t(length) * 16);
        size_t used = 0;
        while (used < metadata.size()) {
            size_t n = body(metadata.data() + used, metadata.size() - used);
            if (!n)
                throw std::runtime_error("Truncated ICY metadata");
            used += n;
        }
        if (!metadata.empty())
            owner.emit("META", std::move(metadata));
        metaRemaining = interval;
    }
    {
        size_t n = body(out, interval ? std::min(size, size_t(metaRemaining)) : size);
        if (n) {
            track->received += n;
            if (interval)
                metaRemaining -= unsigned(n);
            return n;
        }
    }
eof:
    complete();
    return 0;
}
void HttpStream::complete() {
    if (!disconnectSent && !track->networkEnded) {
        track->networkEnded = true;
        disconnectSent = true;
        owner.emit("DSCO", Bytes{0});
    }
}
void HttpStream::exact(void *out, size_t size) {
    auto p = static_cast<uint8_t *>(out);
    while (size) {
        size_t n = read(p, size);
        if (!n)
            throw std::runtime_error("Truncated audio");
        size -= n;
        p += n;
    }
}
void HttpStream::discard(uint64_t size) {
    std::array<uint8_t, 4096> data;
    while (size) {
        size_t n = size_t(std::min<uint64_t>(size, data.size()));
        exact(data.data(), n);
        size -= n;
    }
}
} // namespace player
