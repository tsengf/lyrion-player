/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PLAYER_NATIVE_AUDIO_H
#define PLAYER_NATIVE_AUDIO_H

#define PLAYER_SAMPLE_RATE 48000
typedef void (*player_audio_pull)(void *samples, unsigned frames, unsigned latency_frames);
typedef void (*player_audio_status)(const char *message);
/* Starts a worker; missing hardware is retried without disconnecting from LMS. */
int player_audio_start(player_audio_pull pull, player_audio_status status);
/* Joins the worker and stops all callbacks before returning. */
void player_audio_stop(void);
#endif
