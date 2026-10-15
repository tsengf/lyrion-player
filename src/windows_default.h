/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PLAYER_WINDOWS_DEFAULT_H
#define PLAYER_WINDOWS_DEFAULT_H
/* All calls must run on the same worker thread (COM apartment ownership). */
void *player_default_monitor_create(void);
int player_default_monitor_changed(void *monitor);
void player_default_monitor_destroy(void *monitor);
#endif
