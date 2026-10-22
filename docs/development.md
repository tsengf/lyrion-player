# Development and validation

Build commands are in the [README](../README.md#build-from-source). Run the CTest command
for your platform after building.

## Automated coverage

`ctest` uses a separate `player_test_audio` executable with a silent backend. It
tests a SlimProto handshake against a local mock server, HTTP PCM streaming,
44.1-to-48 kHz resampling, an advancing playback clock, pause, and rejection of
fixed-device output. It also tests localhost discovery against an isolated mock
server without `--server`, including rejection of invalid discovery replies. Discovery
tests include replies received only on a separate source-bound interface socket.
They use a temporary port and simulated interface through test-only overrides;
production always probes
port 3483. Tests never play sound or connect to your real music server.
Broadcast calculation tests also cover different subnet sizes and reject masks
without a usable subnet broadcast. For a manual UDP-only check against real
servers, run `./build/player_discovery_test --probe`; it logs the interface probes
and first replying server without registering a player or playing audio.
Managed-mode tests verify JSON status, graceful stop, stdin EOF, and connected
shutdown against mock servers. Shared tray tests cover configuration, identities,
independent lifecycle, bounded restarts and compatibility with older configurations.
Native process tests exercise real inherited pipes, independent stop and force termination with the
silent backend. Qt Test exercises server selection, automatic address previews, manual edits,
validation, persisted identities, selection during refresh, window resizing,
editor lifetime and About. App smoke tests create the settings/editor controls using a
temporary profile. Metadata discovery tests collect multiple mock replies. None of
these tests modify sign-in registrations.
A CI workflow builds/tests/packages the Qt apps on macOS and Windows using
the default CMake `package` target and CPack. It mounts the macOS DMG, checks the
Applications shortcut, in-app licenses and signature, and runs both packaged executables in smoke/help
mode. On Windows it installs the NSIS package into a temporary directory, checks
runtime DLLs and the Start menu shortcut, smoke-tests both executables, then
uninstalls it. Local package commands are in the README.

Independent-backend tests additionally cover fragmented/coalesced control frames,
maximum-length and malformed packets, reconnection, player-name settings, timer
tokens, FLAC/MP3/Vorbis decoding, WAV/AIFF and high-rate PCM, chunked HTTP, ICY
metadata, deferred codec/continuation commands, queued tracks, decoder errors,
and cancellation while an HTTP body or continuation is blocked. Sample-level
checks verify volume, replay gain, clipping, polarity, channel selection, pause,
power, timed starts, skip, fades/crossfades, and track boundaries without an audio device.
`tests/fixtures` contains generated one-second tones, so codec tests need no
encoder installation. See its README for regeneration commands.

Physical device switching requires manual validation on both systems:

1. Start a track in Lyrion; change the system output between speakers and a
   USB/Bluetooth device. Confirm audio moves and the same track remains active.
2. Switch while paused, then resume; confirm output uses the new default.
3. Disconnect the active output; reconnect it or select another output. Confirm
   recovery without restarting Player.
4. Switch between devices configured for different sample rates, skip tracks,
   adjust volume, and test sleep/wake.
5. Enable Start app at login, sign out/in, verify configured players appear, then
   disable Start app at login.

Hardware rerouting and real sign-in registration require manual verification on
each target system. See [architecture](architecture.md) for the ownership and
recovery model.

## UI development

The tray frontend requires Qt 6.7 or later (Widgets and Network), plus Qt Test when
`PLAYER_BUILD_TESTS=ON`. Backend-only builds require no Qt. On Windows, deploy Qt
next to the test executable before CTest as described in the README. macOS UI unit
tests use Qt's offscreen platform; the application smoke test uses Cocoa.

To refresh the README screenshots on macOS with Qt 6.8 or later, run:

```bash
PLAYER_TEST_SCREENSHOT_DIR="$PWD/docs/images" ./build/player_qt_ui_test screenshots
```

The fixture uses temporary configuration and example addresses. It never starts
players or changes the system appearance. Inspect both exported themes before
committing them. Real tray interaction, screen placement, keyboard navigation,
sign-in and device switching still need manual checks on each platform.
