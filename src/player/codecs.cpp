// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback.hpp"
#include <FLAC/stream_decoder.h>
#include <mpg123.h>
#include <soxr.h>
#include <vorbis/vorbisfile.h>
#ifdef PLAYER_AAC
#include <neaacdec.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
namespace player {
namespace {
int32_t scaled(double sample) {
    if (!std::isfinite(sample))
        return 0;
    return int32_t(std::clamp(sample * 2147483648.0, double(INT32_MIN), double(INT32_MAX)));
}
class Sink {
    Playback &owner;
    std::shared_ptr<Track> track;
    soxr_t resampler = nullptr;
    unsigned rate = 0;
    std::array<int32_t, 8192> output{};

  public:
    Sink(Playback &o, std::shared_ptr<Track> t) : owner(o), track(std::move(t)) {}
    ~Sink() {
        if (resampler)
            soxr_delete(resampler);
    }
    void setRate(unsigned r) {
        if (!r || r > 768000)
            throw std::runtime_error("Invalid audio sample rate");
        if (r == rate)
            return;
        finish();
        if (resampler) {
            soxr_delete(resampler);
            resampler = nullptr;
        }
        rate = r;
        if (rate != 48000) {
            soxr_error_t error = nullptr;
            auto io = soxr_io_spec(SOXR_INT32_I, SOXR_INT32_I);
            auto quality = soxr_quality_spec(SOXR_HQ, 0);
            resampler = soxr_create(rate, 48000, 2, &error, &io, &quality, nullptr);
            if (error || !resampler)
                throw std::runtime_error(error ? error : "Cannot create resampler");
            owner.log("resampling from " + std::to_string(rate) + " -> 48000");
        }
    }
    void write(const int32_t *data, size_t frames) {
        if (!rate)
            throw std::runtime_error("Missing sample rate");
        if (!resampler) {
            owner.write(track, data, frames);
            return;
        }
        while (frames) {
            size_t used = 0, produced = 0;
            auto error = soxr_process(resampler, data, frames, &used, output.data(),
                                      output.size() / 2, &produced);
            if (error)
                throw std::runtime_error(error);
            if (!used && !produced)
                throw std::runtime_error("Resampler made no progress");
            owner.write(track, output.data(), produced);
            data += used * 2;
            frames -= used;
        }
    }
    void finish() {
        if (!resampler)
            return;
        size_t produced;
        do {
            auto error = soxr_process(resampler, nullptr, 0, nullptr, output.data(),
                                      output.size() / 2, &produced);
            if (error)
                throw std::runtime_error(error);
            owner.write(track, output.data(), produced);
        } while (produced);
    }
};
uint32_t integer(const uint8_t *p, unsigned bytes, bool little) {
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
        value = (value << 8) | p[little ? bytes - i - 1 : i];
    return value;
}
void pcm(HttpStream &stream, const Format &f, Sink &sink) {
    static constexpr unsigned rates[] = {11025, 22050, 32000, 44100,  48000,  8000,   12000, 16000,
                                         24000, 96000, 88200, 176400, 192000, 352800, 384000};
    unsigned bits = (f.size >= '0' && f.size <= '3') ? unsigned(f.size - '0' + 1) * 8 : 0;
    unsigned index = f.rate >= '0' ? unsigned(f.rate - '0') : 99;
    unsigned rate = index < std::size(rates) ? rates[index] : 0;
    unsigned channels = f.channels == '1' ? 1 : f.channels == '2' ? 2 : 0;
    bool little = f.endian == '1', unsigned8 = false, floating = false;
    uint64_t length = UINT64_MAX;
    std::array<uint8_t, 16384> input{};
    size_t buffered = 0;
    while (buffered < 12) {
        size_t n = stream.read(input.data() + buffered, 12 - buffered);
        if (!n)
            break;
        buffered += n;
    }
    bool wave = buffered == 12 && !std::memcmp(input.data(), "RIFF", 4) &&
                !std::memcmp(input.data() + 8, "WAVE", 4);
    bool aiff =
        buffered == 12 && !std::memcmp(input.data(), "FORM", 4) &&
        (!std::memcmp(input.data() + 8, "AIFF", 4) || !std::memcmp(input.data() + 8, "AIFC", 4));
    if (wave || aiff) {
        buffered = 0;
        bool format = false;
        // Bound header scanning, including unknown chunks, to 16 MiB.
        uint64_t scanned = 0;
        for (;;) {
            uint8_t h[8];
            stream.exact(h, 8);
            uint32_t size = integer(h + 4, 4, wave);
            if ((wave && !std::memcmp(h, "data", 4)) || (aiff && !std::memcmp(h, "SSND", 4))) {
                if (!format)
                    throw std::runtime_error("PCM data precedes format");
                length = wave && size == UINT32_MAX ? UINT64_MAX : size;
                if (aiff) {
                    uint8_t ssnd[8];
                    if (size < 8)
                        throw std::runtime_error("Short SSND");
                    stream.exact(ssnd, 8);
                    uint32_t offset = integer(ssnd, 4, false);
                    if (offset > size - 8)
                        throw std::runtime_error("Invalid SSND offset");
                    stream.discard(offset);
                    length = size - 8 - offset;
                }
                break;
            }
            scanned += uint64_t(size) + 8;
            if (scanned > 16 * 1024 * 1024)
                throw std::runtime_error("PCM header too large");
            if (wave && !std::memcmp(h, "fmt ", 4)) {
                if (size < 16 || size > 4096)
                    throw std::runtime_error("Invalid WAV format");
                Bytes fmt(size);
                stream.exact(fmt.data(), size);
                unsigned type = integer(fmt.data(), 2, true);
                if (type == 0xfffe && size >= 40)
                    type = integer(fmt.data() + 24, 2, true);
                if (type != 1 && type != 3)
                    throw std::runtime_error("Unsupported WAV encoding");
                floating = type == 3;
                channels = integer(fmt.data() + 2, 2, true);
                rate = integer(fmt.data() + 4, 4, true);
                bits = integer(fmt.data() + 14, 2, true);
                little = true;
                unsigned8 = true;
                format = true;
            } else if (aiff && !std::memcmp(h, "COMM", 4)) {
                if (size < 18 || size > 4096)
                    throw std::runtime_error("Invalid AIFF format");
                Bytes fmt(size);
                stream.exact(fmt.data(), size);
                channels = integer(fmt.data(), 2, false);
                bits = integer(fmt.data() + 6, 2, false);
                unsigned exponent = integer(fmt.data() + 8, 2, false);
                double mantissa = double(integer(fmt.data() + 10, 4, false)) * 4294967296.0 +
                                  integer(fmt.data() + 14, 4, false);
                double sampleRate = std::ldexp(mantissa, int(exponent & 0x7fff) - 16383 - 63);
                if ((exponent & 0x8000) || !std::isfinite(sampleRate) || sampleRate < 1 ||
                    sampleRate > 768000)
                    throw std::runtime_error("Invalid AIFF sample rate");
                rate = unsigned(sampleRate);
                little = false;
                if (size >= 22) {
                    little = !std::memcmp(fmt.data() + 18, "sowt", 4);
                    if (!little && std::memcmp(fmt.data() + 18, "NONE", 4))
                        throw std::runtime_error("Unsupported AIFC encoding");
                }
                format = true;
            } else
                stream.discard(size);
            if (size & 1)
                stream.discard(1);
        }
    }
    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        (floating && bits != 32))
        throw std::runtime_error("Unsupported PCM format");
    sink.setRate(rate);
    size_t frameBytes = channels * (bits / 8);
    std::array<int32_t, 8192> output;
    for (;;) {
        size_t frames = std::min(buffered / frameBytes, output.size() / 2);
        if (length != UINT64_MAX)
            frames = std::min<uint64_t>(frames, length / frameBytes);
        for (size_t i = 0; i < frames; ++i) {
            for (unsigned ch = 0; ch < 2; ++ch) {
                const uint8_t *p =
                    input.data() + (i * channels + std::min(ch, channels - 1)) * (bits / 8);
                uint32_t value = integer(p, bits / 8, little);
                if (floating) {
                    float v;
                    std::memcpy(&v, &value, 4);
                    output[i * 2 + ch] = std::isfinite(v) ? scaled(v) : 0;
                } else if (bits == 8 && unsigned8)
                    output[i * 2 + ch] = int32_t((int(value) - 128) * 16777216);
                else {
                    uint32_t aligned = value << (32 - bits);
                    std::memcpy(&output[i * 2 + ch], &aligned, 4);
                }
            }
        }
        sink.write(output.data(), frames);
        size_t used = frames * frameBytes;
        buffered -= used;
        std::memmove(input.data(), input.data() + used, buffered);
        if (length != UINT64_MAX) {
            length -= used;
            if (!length)
                break;
            if (length < frameBytes)
                throw std::runtime_error("Partial PCM frame");
        }
        size_t size = std::min(input.size() - buffered, output.size() / 2 * frameBytes);
        if (length != UINT64_MAX)
            size = std::min<uint64_t>(size, length - buffered);
        size_t n = stream.read(input.data() + buffered, size);
        if (!n) {
            if (buffered || length != UINT64_MAX)
                throw std::runtime_error("Truncated PCM data");
            break;
        }
        buffered += n;
    }
}
struct FlacContext {
    HttpStream &stream;
    Sink &sink;
    std::exception_ptr exception;
    bool error = false;
};
FLAC__StreamDecoderReadStatus flacRead(const FLAC__StreamDecoder *, FLAC__byte *data, size_t *size,
                                       void *context) {
    auto &c = *static_cast<FlacContext *>(context);
    try {
        *size = c.stream.read(data, *size);
        return *size ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE
                     : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    } catch (...) {
        c.exception = std::current_exception();
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
}
FLAC__StreamDecoderWriteStatus flacWrite(const FLAC__StreamDecoder *, const FLAC__Frame *f,
                                         const FLAC__int32 *const data[], void *context) {
    auto &c = *static_cast<FlacContext *>(context);
    try {
        if (f->header.channels < 1 || f->header.channels > 2 || !f->header.bits_per_sample ||
            f->header.bits_per_sample > 32)
            throw std::runtime_error("Unsupported FLAC format");
        c.sink.setRate(f->header.sample_rate);
        std::vector<int32_t> pcm(size_t(f->header.blocksize) * 2);
        int64_t factor = int64_t(1) << (32 - f->header.bits_per_sample);
        for (size_t i = 0; i < f->header.blocksize; ++i)
            for (unsigned ch = 0; ch < 2; ++ch)
                pcm[i * 2 + ch] =
                    int32_t(int64_t(data[std::min(ch, f->header.channels - 1)][i]) * factor);
        c.sink.write(pcm.data(), f->header.blocksize);
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    } catch (...) {
        c.exception = std::current_exception();
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
}
void flacError(const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus, void *context) {
    static_cast<FlacContext *>(context)->error = true;
}
void flac(HttpStream &stream, const Format &f, Sink &sink) {
    auto decoder = FLAC__stream_decoder_new();
    if (!decoder)
        throw std::bad_alloc();
    FlacContext context{stream, sink, {}, false};
    auto init =
        f.size == 'o' ? FLAC__stream_decoder_init_ogg_stream : FLAC__stream_decoder_init_stream;
    auto status = init(decoder, flacRead, nullptr, nullptr, nullptr, nullptr, flacWrite, nullptr,
                       flacError, &context);
    bool ok = status == FLAC__STREAM_DECODER_INIT_STATUS_OK &&
              FLAC__stream_decoder_process_until_end_of_stream(decoder);
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    if (context.exception)
        std::rethrow_exception(context.exception);
    if (!ok || context.error)
        throw std::runtime_error("FLAC decoding failed");
}
void mp3(HttpStream &stream, Sink &sink) {
    int error;
    mpg123_handle *h = mpg123_new(nullptr, &error);
    if (!h)
        throw std::runtime_error("Cannot create MP3 decoder");
    try {
        mpg123_format_none(h);
        const long *rates;
        size_t count;
        mpg123_rates(&rates, &count);
        for (size_t i = 0; i < count; ++i)
            mpg123_format(h, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_32);
        if (mpg123_open_feed(h) != MPG123_OK)
            throw std::runtime_error("Cannot open MP3 decoder");
        std::array<uint8_t, 16384> input;
        std::array<int32_t, 8192> decoded, stereo;
        int channels = 0, encoding = 0;
        long rate = 0;
        for (;;) {
            size_t n = stream.read(input.data(), input.size());
            if (!n)
                break;
            if (mpg123_feed(h, input.data(), n) != MPG123_OK)
                throw std::runtime_error("MP3 input failed");
            for (;;) {
                size_t bytes = 0;
                int result = mpg123_read(h, reinterpret_cast<unsigned char *>(decoded.data()),
                                         sizeof(decoded), &bytes);
                if (result == MPG123_NEW_FORMAT) {
                    if (mpg123_getformat(h, &rate, &channels, &encoding) != MPG123_OK ||
                        (channels != 1 && channels != 2) || encoding != MPG123_ENC_SIGNED_32)
                        throw std::runtime_error("Unsupported MP3 output");
                    sink.setRate(unsigned(rate));
                }
                if (bytes) {
                    size_t frames = bytes / (sizeof(int32_t) * size_t(channels));
                    if (channels == 2)
                        sink.write(decoded.data(), frames);
                    else {
                        for (size_t offset = 0; offset < frames;) {
                            size_t chunk = std::min(frames - offset, stereo.size() / 2);
                            for (size_t i = 0; i < chunk; ++i)
                                stereo[i * 2] = stereo[i * 2 + 1] = decoded[offset + i];
                            sink.write(stereo.data(), chunk);
                            offset += chunk;
                        }
                    }
                }
                if (result == MPG123_NEED_MORE || result == MPG123_DONE)
                    break;
                if (result != MPG123_OK && result != MPG123_NEW_FORMAT)
                    throw std::runtime_error(mpg123_strerror(h));
            }
        }
    } catch (...) {
        mpg123_delete(h);
        throw;
    }
    mpg123_delete(h);
}
struct VorbisContext {
    HttpStream &stream;
    std::exception_ptr exception;
};
size_t vorbisRead(void *data, size_t size, size_t count, void *source) {
    auto &c = *static_cast<VorbisContext *>(source);
    try {
        if (!size || count > SIZE_MAX / size)
            return 0;
        size_t used = 0, total = size * count;
        while (used < total) {
            size_t n = c.stream.read(static_cast<uint8_t *>(data) + used, total - used);
            if (!n)
                break;
            used += n;
        }
        return used / size;
    } catch (...) {
        c.exception = std::current_exception();
        return 0;
    }
}
void vorbis(HttpStream &stream, Sink &sink) {
    VorbisContext context{stream, {}};
    OggVorbis_File file{};
    ov_callbacks callbacks{vorbisRead, nullptr, nullptr, nullptr};
    if (ov_open_callbacks(&context, &file, nullptr, 0, callbacks) < 0) {
        if (context.exception)
            std::rethrow_exception(context.exception);
        throw std::runtime_error("Cannot open Vorbis stream");
    }
    try {
        int section = 0;
        std::array<int32_t, 4096> pcm;
        for (;;) {
            float **channels = nullptr;
            long frames = ov_read_float(&file, &channels, int(pcm.size() / 2), &section);
            if (context.exception)
                std::rethrow_exception(context.exception);
            if (frames == 0)
                break;
            if (frames < 0)
                throw std::runtime_error("Corrupt Vorbis stream");
            auto info = ov_info(&file, -1);
            if (!info || info->channels < 1 || info->channels > 2)
                throw std::runtime_error("Unsupported Vorbis channels");
            sink.setRate(unsigned(info->rate));
            for (long i = 0; i < frames; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    pcm[size_t(i) * 2 + ch] = scaled(channels[std::min(ch, info->channels - 1)][i]);
            sink.write(pcm.data(), size_t(frames));
        }
    } catch (...) {
        ov_clear(&file);
        throw;
    }
    ov_clear(&file);
}
#ifdef PLAYER_AAC
void aac(HttpStream &stream, Sink &sink) {
    auto h = NeAACDecOpen();
    if (!h)
        throw std::bad_alloc();
    try {
        auto config = NeAACDecGetCurrentConfiguration(h);
        config->outputFormat = FAAD_FMT_16BIT;
        if (!NeAACDecSetConfiguration(h, config))
            throw std::runtime_error("AAC configuration failed");
        std::array<unsigned char, 65536> input;
        size_t used = 0;
        bool initialized = false, eof = false;
        while (!eof || used) {
            if (!eof && used < input.size()) {
                size_t n = stream.read(input.data() + used, input.size() - used);
                used += n;
                eof = !n;
            }
            if (!initialized) {
                if (used < 8192 && !eof)
                    continue;
                unsigned long rate;
                unsigned char channels;
                long consumed = NeAACDecInit(h, input.data(), static_cast<unsigned long>(used),
                                             &rate, &channels);
                if (consumed < 0)
                    throw std::runtime_error("Only ADTS/ADIF AAC is supported");
                used -= size_t(consumed);
                std::memmove(input.data(), input.data() + consumed, used);
                initialized = true;
            }
            if (!used)
                break;
            NeAACDecFrameInfo info{};
            auto data = static_cast<int16_t *>(
                NeAACDecDecode(h, &info, input.data(), static_cast<unsigned long>(used)));
            if (info.error || !info.bytesconsumed || info.bytesconsumed > used)
                throw std::runtime_error("AAC decoding failed");
            used -= info.bytesconsumed;
            std::memmove(input.data(), input.data() + info.bytesconsumed, used);
            if (info.samples) {
                if (!data || info.channels < 1 || info.channels > 2)
                    throw std::runtime_error("Unsupported AAC channels");
                sink.setRate(unsigned(info.samplerate));
                size_t frames = info.samples / info.channels;
                std::vector<int32_t> pcm(frames * 2);
                for (size_t i = 0; i < frames; ++i)
                    for (unsigned ch = 0; ch < 2; ++ch)
                        pcm[i * 2 + ch] = int32_t(data[i * info.channels +
                                                       std::min(ch, unsigned(info.channels - 1))]) *
                                          65536;
                sink.write(pcm.data(), frames);
            }
        }
    } catch (...) {
        NeAACDecClose(h);
        throw;
    }
    NeAACDecClose(h);
}
#endif
} // namespace
void decode(HttpStream &stream, const Format &f, Playback &owner,
            const std::shared_ptr<Track> &track) {
    Sink sink(owner, track);
    switch (f.codec) {
    case 'p':
        pcm(stream, f, sink);
        break;
    case 'f':
        flac(stream, f, sink);
        break;
    case 'm':
        mp3(stream, sink);
        break;
    case 'o':
        vorbis(stream, sink);
        break;
#ifdef PLAYER_AAC
    case 'a':
        aac(stream, sink);
        break;
#endif
    default:
        throw std::runtime_error("Unsupported codec");
    }
    sink.finish();
    if (!track->produced)
        throw std::runtime_error("No decoded audio");
}
} // namespace player
