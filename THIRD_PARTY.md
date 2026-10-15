# Third-party source

## Squeezelite

- Source: https://github.com/ralph-irving/squeezelite
- Revision: `4e0ec3188320da7de059c571499e626ff8b3712f`
- License: GPL-3.0-or-later, retained in `vendor/squeezelite/LICENSE.txt`
- Imported root C/header files and upstream README; optional upstream backends
  remain available as reference but are not compiled by Player.
- Local changes to `main.c`: identify the executable as Player, default the
  resampling recipe to `hX`, and adjust help for the native backend. The build
  defines `MODEL_NAME=Player`.
- Player uses its own native output adapter in place of `output_pa.c`.

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
The CMake fallback builds upstream FLAC tag `1.5.0` if FLAC is not installed.
