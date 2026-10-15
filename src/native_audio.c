/* SPDX-License-Identifier: GPL-3.0-or-later */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "native_audio.h"
#if defined(_WIN32) && !defined(PLAYER_NULL_AUDIO)
#include "windows_default.h"
#endif

static struct {
    ma_thread thread;
    ma_device device;
    ma_uint32 quit;
    ma_uint32 recover;
    ma_uint32 rerouted;
    player_audio_pull pull;
    player_audio_status status;
    int started;
#if defined(PLAYER_NULL_AUDIO)
    ma_uint32 nonzero_samples;
#endif
} audio;

static void data_callback(ma_device *device, void *out, const void *in, ma_uint32 frames)
{
    (void)in;
    /* Express estimated backend buffering in our fixed 48 kHz clock. */
    unsigned latency = (unsigned)((ma_uint64)device->playback.internalPeriodSizeInFrames *
        device->playback.internalPeriods * PLAYER_SAMPLE_RATE /
        (device->playback.internalSampleRate ? device->playback.internalSampleRate : PLAYER_SAMPLE_RATE));
    audio.pull(out, frames, latency);
#if defined(PLAYER_NULL_AUDIO)
    /* Observe the final output boundary without playing sound. */
    for (ma_uint32 i = 0; i < frames * 2; ++i) {
        if (((ma_int32 *)out)[i] != 0) {
            ma_atomic_store_32(&audio.nonzero_samples, 1);
            break;
        }
    }
#endif
}

static void notification_callback(const ma_device_notification *notification)
{
    /* Never stop/reinitialize here: notifications can hold backend locks. */
    if (notification->type == ma_device_notification_type_stopped)
        ma_atomic_store_32(&audio.recover, 1);
    if (notification->type == ma_device_notification_type_rerouted)
        ma_atomic_store_32(&audio.rerouted, 1);
}

static ma_thread_result worker(void *unused)
{
    int initialized = 0;
    int retry_ticks = 0;
    int reported_failure = 0;
#if defined(PLAYER_NULL_AUDIO)
    int reported_samples = 0;
#endif
#if defined(_WIN32) && !defined(PLAYER_NULL_AUDIO)
    void *monitor = NULL;
    int monitor_retry = 0;
#endif
    (void)unused;
    while (!ma_atomic_load_32(&audio.quit)) {
#if defined(PLAYER_NULL_AUDIO)
        if (!reported_samples && ma_atomic_load_32(&audio.nonzero_samples)) {
            audio.status("Test backend received nonzero PCM samples");
            reported_samples = 1;
        }
#endif
#if defined(_WIN32) && !defined(PLAYER_NULL_AUDIO)
        if (!monitor) {
            if (monitor_retry-- <= 0) {
                monitor = player_default_monitor_create();
                if (!monitor) audio.status("Cannot monitor the default audio output; retrying");
                monitor_retry = 20;
            }
            if (!monitor) { ma_sleep(100); continue; }
        }
        if (player_default_monitor_changed(monitor)) {
            audio.status("System default output changed; reopening on the audio worker");
            ma_atomic_store_32(&audio.recover, 1);
        }
#endif
        if (ma_atomic_exchange_32(&audio.rerouted, 0))
            audio.status("Audio now follows the new system default output");
        if (ma_atomic_exchange_32(&audio.recover, 0) && initialized) {
            ma_device_uninit(&audio.device);
            /* Uninit may itself emit stopped; discard that intentional stop. */
            ma_atomic_store_32(&audio.recover, 0);
            initialized = 0;
            retry_ticks = 0;
        }
        if (!initialized && retry_ticks-- <= 0) {
            ma_device_config config = ma_device_config_init(ma_device_type_playback);
            ma_backend backend;
#if defined(PLAYER_NULL_AUDIO)
            backend = ma_backend_null;
#elif defined(_WIN32)
            backend = ma_backend_wasapi;
#else
            backend = ma_backend_coreaudio;
#endif
            config.playback.format = ma_format_s32;
            config.playback.channels = 2;
            config.playback.shareMode = ma_share_mode_shared;
            config.playback.pDeviceID = NULL; /* Required for native default-device routing. */
            config.sampleRate = PLAYER_SAMPLE_RATE;
            config.dataCallback = data_callback;
            config.notificationCallback = notification_callback;
            /* Avoid miniaudio 0.11's blocking WASAPI reroute notification path.
             * The worker observes the native endpoint ID and recreates the device. */
            config.wasapi.noAutoStreamRouting = MA_TRUE;
            /* ma_device_init_ex owns a fresh context on every retry (no stale device list). */
            if (ma_device_init_ex(&backend, 1, NULL, &config, &audio.device) == MA_SUCCESS) {
                initialized = 1;
                if (ma_device_start(&audio.device) != MA_SUCCESS) {
                    ma_device_uninit(&audio.device);
                    initialized = 0;
                }
            }
            if (initialized) {
                audio.status("Playing through the system default output (shared mode)");
                reported_failure = 0;
            } else if (!reported_failure) {
                audio.status("No usable audio output; retrying every 2 seconds");
                reported_failure = 1;
            }
            retry_ticks = 20;
        }
        ma_sleep(100);
    }
    if (initialized) ma_device_uninit(&audio.device);
#if defined(_WIN32) && !defined(PLAYER_NULL_AUDIO)
    if (monitor) player_default_monitor_destroy(monitor);
#endif
    return 0;
}

int player_audio_start(player_audio_pull pull, player_audio_status status)
{
    memset(&audio, 0, sizeof(audio));
    audio.pull = pull;
    audio.status = status;
    if (ma_thread_create(&audio.thread, ma_thread_priority_normal, 0, worker, NULL, NULL) != MA_SUCCESS)
        return 0;
    audio.started = 1;
    return 1;
}

void player_audio_stop(void)
{
    if (!audio.started) return;
    ma_atomic_store_32(&audio.quit, 1);
    ma_thread_wait(&audio.thread);
    audio.started = 0;
}
