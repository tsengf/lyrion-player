/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PLAYER_MANAGED_H
#define PLAYER_MANAGED_H
void player_managed_enable(void);
void player_managed_start(void);
int player_managed_enabled(void);
int player_managed_stopping(void);
void player_managed_connection(const char *state, const char *host, unsigned port);
void player_managed_audio(int available);
#endif
