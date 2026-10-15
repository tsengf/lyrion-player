/* SPDX-License-Identifier: GPL-3.0-or-later */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <stdlib.h>
#include <wchar.h>
#include "windows_default.h"

/* Local GUIDs keep the build independent of SDK uuid library variants. */
static const CLSID player_enumerator_clsid =
    {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const IID player_enumerator_iid =
    {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};

struct default_monitor {
    IMMDeviceEnumerator *enumerator;
    LPWSTR previous_id;
    int have_snapshot;
};

void *player_default_monitor_create(void)
{
    struct default_monitor *monitor;
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return NULL;
    monitor = calloc(1, sizeof(*monitor));
    if (!monitor) {
        CoUninitialize();
        return NULL;
    }
    if (FAILED(CoCreateInstance(&player_enumerator_clsid, NULL, CLSCTX_ALL,
            &player_enumerator_iid, (void **)&monitor->enumerator))) {
        free(monitor);
        CoUninitialize();
        return NULL;
    }
    return monitor;
}

int player_default_monitor_changed(void *opaque)
{
    struct default_monitor *monitor = opaque;
    IMMDevice *device = NULL;
    LPWSTR id = NULL;
    int changed;
    HRESULT result = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
        monitor->enumerator, eRender, eConsole, &device);
    if (SUCCEEDED(result)) {
        result = IMMDevice_GetId(device, &id);
        IMMDevice_Release(device);
        if (FAILED(result)) return 0;
    } else if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        return 0; /* Do not treat transient query errors as output changes. */
    }
    changed = monitor->have_snapshot &&
        ((id == NULL) != (monitor->previous_id == NULL) ||
         (id && monitor->previous_id && wcscmp(id, monitor->previous_id)));
    CoTaskMemFree(monitor->previous_id);
    monitor->previous_id = id;
    monitor->have_snapshot = 1;
    return changed;
}

void player_default_monitor_destroy(void *opaque)
{
    struct default_monitor *monitor = opaque;
    CoTaskMemFree(monitor->previous_id);
    IMMDeviceEnumerator_Release(monitor->enumerator);
    free(monitor);
    CoUninitialize();
}
