# Third-party source

## Squeezelite

- Source: https://github.com/ralph-irving/squeezelite
- Revision: `4e0ec3188320da7de059c571499e626ff8b3712f`
- License: GPL-3.0-or-later, retained in `vendor/squeezelite/LICENSE.txt`
- Imported root C/header files and upstream README; optional upstream backends
  remain available as reference but are not compiled by Player.
- Local changes to `main.c`: identify the executable as Player, default the
  resampling recipe to `hX`, add long command-line aliases, and adjust help for
  the native backend, and add managed-mode IPC. The build defines `MODEL_NAME=Player`.
- Player uses its own native output adapter in place of `output_pa.c`.
- Local changes to `slimproto.c`: use Player's discovery adapter for localhost,
  general broadcast, and source-bound per-interface LAN probes, and allow an
  isolated discovery port in the silent test build only; managed mode also reports
  connection state and accepts cancellation during discovery/retries.

## Miniaudio

- Source: https://github.com/mackron/miniaudio
- Version: 0.11.23, revision `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`
- Unmodified `miniaudio.h` vendored in `vendor/miniaudio`.
- License: MIT No Attribution option, full text at the end of the header.
- Player uses internal miniaudio atomic/thread utilities in the same translation
  unit as the implementation; review those interfaces when updating the pin.

## Linked dependencies

FLAC and Ogg/Vorbis use BSD-style licenses; mpg123 uses LGPL-2.1; soxr uses
LGPL-2.1-or-later. Optional faad2 uses GPL-2.0-or-later. Preserve the corresponding
license notices and meet source/relinking obligations when distributing binaries.
Baseline codec notices are retained in `vendor/licenses` and included in Tray
packages (FLAC 1.5.0 COPYING.Xiph, installed Ogg/Vorbis COPYING and soxr LICENCE,
and upstream mpg123 COPYING). Distributors must also retain notices for any
additional features/dependencies of their chosen codec builds. Windows packages
include the Visual C++ runtime files selected by CMake.
The CMake fallback builds upstream FLAC tag `1.5.0` if FLAC is not installed.

## nlohmann/json

- Source: https://github.com/nlohmann/json
- Version: 3.12.0, single header at `vendor/json/json.hpp`.
- SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`.
- License: MIT, retained in `vendor/json/LICENSE.MIT`.
- Used by the shared tray manager for configuration and backend status messages.
