/* SPDX-License-Identifier: GPL-3.0-or-later
 * Player's native audio adapter for the Squeezelite output engine.
 */
#include "squeezelite.h"
#include "native_audio.h"
#include "managed.h"

static log_level loglevel;
static u8_t *write_pointer;
extern struct outputstate output;
extern struct buffer *outputbuf;

void list_devices(void)
{
    printf("default - System default output (automatically follows changes)\n");
}

bool test_open(const char *device, unsigned rates[], bool userdef_rates)
{
    (void)userdef_rates;
    if (strcmp(device, "default")) return false;
    rates[0] = PLAYER_SAMPLE_RATE;
    rates[1] = 0;
    return true; /* Hardware can arrive after the network connection. */
}

void set_volume(unsigned left, unsigned right)
{
    mutex_lock(outputbuf->mutex);
    output.gainL = left;
    output.gainR = right;
    mutex_unlock(outputbuf->mutex);
}

/* SlimProto calls this with its output lock held. Our clock stays at 48 kHz. */
void _pa_open(void) {}

static int write_frames(frames_t frames, bool silence, s32_t gainL, s32_t gainR,
    u8_t flags, s32_t cross_gain_in, s32_t cross_gain_out, s32_t **cross_ptr)
{
    if (silence) {
        memset(write_pointer, 0, frames * BYTES_PER_FRAME);
    } else {
        if (output.fade == FADE_ACTIVE && output.fade_dir == FADE_CROSS && *cross_ptr)
            _apply_cross(outputbuf, frames, cross_gain_in, cross_gain_out, cross_ptr);
        if (gainL != FIXED_ONE || gainR != FIXED_ONE)
            _apply_gain(outputbuf, frames, gainL, gainR, flags);
        memcpy(write_pointer, outputbuf->readp, frames * BYTES_PER_FRAME);
    }
    write_pointer += frames * BYTES_PER_FRAME;
    return (int)frames;
}

static void pull_audio(void *samples, unsigned frames, unsigned latency)
{
    memset(samples, 0, frames * BYTES_PER_FRAME);
    mutex_lock(outputbuf->mutex);
    write_pointer = samples;
    output.device_frames = latency;
    output.updated = gettime_ms();
    output.frames_played_dmp = output.frames_played;
    while (frames) {
        frames_t consumed = _output_frames(frames);
        if (!consumed) break;
        frames -= consumed;
    }
    mutex_unlock(outputbuf->mutex);
}

static void audio_status(const char *message, int available)
{
    LOG_INFO("%s", message);
    player_managed_audio(available);
}

void output_init_pa(log_level level, const char *device, unsigned buffer_size,
    char *params, unsigned rates[], unsigned rate_delay, unsigned idle)
{
    extern bool user_rates;
    loglevel = level;
    if (strcmp(device, "default") || params || user_rates) {
        LOG_ERROR("Player requires default output and manages its own format; omit -o, -a and -r");
        exit(1);
    }
    memset(&output, 0, sizeof(output));
    output.write_cb = write_frames;
    output.rate_delay = rate_delay;
    output_init_common(level, device, buffer_size, rates, idle);
    if (!player_audio_start(pull_audio, audio_status)) {
        LOG_ERROR("Unable to start audio worker");
        exit(1);
    }
}

void output_close_pa(void)
{
    /* Join callbacks before freeing the ring buffer or its mutex. */
    player_audio_stop();
    output_close_common();
}
