/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "squeezelite.h"
#include "managed.h"

static int enabled, stopping;
static mutex_type state_mutex, event_mutex;
static thread_type reader;

void player_managed_enable(void) {
    if (enabled) return;
    mutex_create(state_mutex);
    mutex_create(event_mutex);
    enabled = 1;
}
int player_managed_enabled(void) { return enabled; }
int player_managed_stopping(void) {
    int value;
    if (!enabled) return 0;
    mutex_lock(state_mutex);
    value = stopping;
    mutex_unlock(state_mutex);
    return value;
}
static void request_stop(void) {
    mutex_lock(state_mutex);
    stopping = 1;
    mutex_unlock(state_mutex);
    wake_controller();
}
#if WIN
static DWORD WINAPI read_commands(LPVOID ignored) {
#else
static void *read_commands(void *ignored) {
#endif
    char command[128];
    (void)ignored;
    while (fgets(command, sizeof(command), stdin)) {
        if (!strcmp(command, "stop\n") || !strcmp(command, "stop\r\n")) break;
    }
    /* EOF means the owning tray app exited, including unexpected termination. */
    request_stop();
    return 0;
}
void player_managed_start(void) {
    if (!enabled) return;
#if WIN
    reader = CreateThread(NULL, 0, read_commands, NULL, 0, NULL);
    if (!reader) request_stop();
#else
    if (pthread_create(&reader, NULL, read_commands, NULL)) request_stop();
    else pthread_detach(reader);
#endif
}
void player_managed_connection(const char *state, const char *host, unsigned port) {
    if (!enabled) return;
    /* Callers supply fixed state strings and numeric IPv4 addresses only. */
    mutex_lock(event_mutex);
    printf("{\"version\":1,\"type\":\"connection\",\"state\":\"%s\",\"host\":\"%s\",\"port\":%u}\n",
           state, host ? host : "", port);
    fflush(stdout);
    mutex_unlock(event_mutex);
}
void player_managed_audio(int available) {
    if (!enabled || available < 0) return;
    mutex_lock(event_mutex);
    printf("{\"version\":1,\"type\":\"audio\",\"state\":\"%s\"}\n",
           available ? "ready" : "unavailable");
    fflush(stdout);
    mutex_unlock(event_mutex);
}
