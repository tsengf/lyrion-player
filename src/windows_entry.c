/* SPDX-License-Identifier: GPL-3.0-or-later */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv);

/* GUI subsystem avoids opening a console window at sign-in. When invoked from
 * a terminal, attach to it while preserving explicitly redirected output. */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR)
            freopen("CONOUT$", "w", stdout);
        if (GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_CHAR)
            freopen("CONOUT$", "w", stderr);
    }
    /* Keep names/configuration UTF-8 even on systems using a legacy code page. */
    int argc = 0, result = 1, i;
    LPWSTR *wide_args = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **args = calloc((size_t)argc + 1, sizeof(char *));
    if (!wide_args || !args) { if (wide_args) LocalFree(wide_args); free(args); return 1; }
    for (i = 0; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wide_args[i], -1, NULL, 0, NULL, NULL);
        args[i] = malloc(size);
        if (!args[i]) goto cleanup;
        WideCharToMultiByte(CP_UTF8, 0, wide_args[i], -1, args[i], size, NULL, NULL);
    }
    result = main(argc, args);
cleanup:
    for (i = 0; i < argc; ++i) free(args[i]);
    free(args); LocalFree(wide_args);
    return result;
}
