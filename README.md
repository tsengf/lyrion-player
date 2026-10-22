# Lyrion Player

Turn a Mac or Windows computer into a playback device for Lyrion Music Server. Manage players from the menu bar or system tray, then control music through Lyrion's web interface or a compatible controller.

[Installation](#installation) · [Usage](#usage) · [Build from source](#build-from-source) · [Build packages](#build-packages) · [Tests](#tests)

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/settings-macos-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/settings-macos.png">
  <img alt="Lyrion Player Settings on macOS" src="docs/images/settings-macos.png">
</picture>

*Settings on macOS, shown in light or dark mode to match your theme. Player names and IP addresses are fictional examples.*

## Features

- **Automatic discovery.** Find servers on your computer and local network, or connect to a chosen host.
- **Multiple players.** Save separate player identities and queues, including players connected to different servers.
- **System audio output.** Follow the signed-in user's default output, including device changes during playback. Shared audio allows other applications to play sound too.
- **Native management.** Add, edit, start and stop players, inspect their status and logs, and open Lyrion from a macOS or Windows interface.
- **Optional startup.** Launch the app at login and choose which players start with it. Closing Settings keeps playback running.
- **Independent playback.** Decode FLAC, MP3, Ogg Vorbis and PCM without Squeezelite. Soxr and miniaudio provide resampling and native audio output.

## Installation

Download an installer from a successful run of the [**Build and test** workflow](https://github.com/tsengf/lyrion-player/actions/workflows/build.yml). Extract the downloaded GitHub Actions artifact to access the package.

| Platform | Artifact | Package | Install |
| --- | --- | --- | --- |
| macOS, Apple Silicon | `lyrion-player-macos-arm64` | `.dmg` | Open the image and drag **Lyrion Player.app** to Applications. |
| macOS, Intel | `lyrion-player-macos-x86_64` | `.dmg` | Open the image and drag **Lyrion Player.app** to Applications. |
| Windows x64 | `lyrion-player-windows-x64` | `.exe` | Run the installer, then open **Lyrion Player** from the Start menu. |

The app targets macOS 13 or later and Windows 10 or later. Bundled codec libraries may require a newer OS, so the app's deployment target alone does not guarantee package compatibility with those minimum versions. Packages include the backend and runtime libraries; Python, Homebrew, MacPorts and vcpkg are not needed to run them.

macOS packages are ad-hoc signed without Apple notarization. Windows installers are unsigned. The operating system may show an unverified-app warning.

For a portable installation, [build a ZIP](#portable-zips) and extract the complete package into a permanent folder. On Windows, keep `player.exe`, the DLLs and licenses beside **Lyrion Player.exe**. Place the app in its permanent location before enabling login startup.

## Usage

Lyrion Player requires a running Lyrion Music Server. The tray app manages playback devices; choose music and control playback in Lyrion or a compatible controller.

### Quick start

1. Open **Settings…** from the menu-bar or tray icon and choose **Add…**.
2. Enter a player name. Choose **Automatic**, a discovered server, or **Manual address** with a host and SlimProto port, normally `3483`.
3. Save, select the player, and choose **Start / Stop**. Select that player in Lyrion and start music.
4. Repeat **Add…** for additional players. Optionally enable **Start app at login** and choose which players start with the app.

Closing Settings keeps playback running. **Quit Lyrion Player** stops all players managed by the app before exiting.

### Player settings

Open **Settings…** from the menu-bar or tray icon to manage players. The table shows each player's configuration, connection and audio status. Info icons explain the fields when adding or editing a player.

| Column | Meaning |
| --- | --- |
| Player | The name displayed in Lyrion. |
| Auto discovery | Yes for Automatic; No for a selected server or manual address. |
| Server | The discovered IP address and SlimProto port, or the configured host and port. Blank until automatic discovery finds a server. |
| Start with app | Yes if the player starts when the tray app opens; otherwise No. |
| Status | The player's current connection and audio status. |

Select a player to **Edit**, **Remove**, **Start / Stop**, **View Log**, or **Open Lyrion**. Actions requiring a selection are greyed out until you select one. Editing a running player restarts that player.

### Server selection

The server list scans automatically at startup and again 15 seconds after each scan completes. **Automatic** selects the first valid server reply. Its read-only host field displays the known server address immediately and updates when discovery completes; saving keeps discovery enabled. The address preview does not pin the player to that server.

Choose a discovered server or **Manual address** to stay attached to a particular server. A discovered selection saves the server's current IP address. If that address changes, select it again or use a stable hostname with Manual address.

**Open Lyrion** uses the discovered web port (default `9000`). Set the optional **Web URL** for a custom HTTPS or proxy address. The web port and SlimProto port serve different purposes; enter the SlimProto port in the server port field.

### Multiple players

Each player connects to one Lyrion server at a time, with its own playback queue. Create separate players to connect to multiple servers. All players use the same system default output; simultaneous playback mixes together.

Player names may repeat, but distinct names make them easier to identify. Each player receives a saved, unique MAC-style **Player ID**. Keep that ID unchanged to preserve its identity and settings in Lyrion, and use a different ID for every player. The tray app manages only the players it launches; it does not adopt players started separately.

### Startup and tray menu

| Setting | Effect | Default |
| --- | --- | --- |
| Start app at login | Opens the tray app when you sign in. | Off |
| Start this instance with the app | Starts that player whenever the tray app opens. | On for new players |

**Only the tray app is registered at login.** Players have no individual LaunchAgents, scheduled tasks, or Windows services. Startup runs in your user session so playback follows the selected output device. Put the app in its permanent location before enabling startup; administrator privileges are not needed.

If macOS requests approval, enable Lyrion Player in System Settings → General → Login Items. See [architecture](docs/architecture.md) for registration and process ownership details.

The tray menu contains **Settings…**, the **Start app at login** toggle, **Start all players**, **Stop all players**, **About Lyrion Player**, and **Quit Lyrion Player**. About shows the version, credits, license, and project website.

### Playback

Native decoding: FLAC, MP3, Ogg Vorbis, and PCM (including WAV/AIFF). Other formats require Lyrion transcoding. Optional ADTS/ADIF AAC support requires faad2 and `-DPLAYER_AAC=ON`; MP4 containers require server transcoding. Direct HTTPS streams are not supported; use the server's HTTP stream/proxy path. This is a stereo PCM player, with no DSD passthrough or bit-perfect/exclusive output.

Decoded music is resampled using soxr to a stable 48 kHz, signed 32-bit stereo stream. Miniaudio adapts that to the selected device's native format. Keeping a stable software clock avoids changing Lyrion's stream capabilities when switching between devices with different sample rates. Resampling defaults to soxr HQ.

Switching devices keeps the same track and server connection; a brief gap while the new output opens is possible. If no usable device exists, Lyrion Player remains connected to Lyrion and retries opening the default output every two seconds. Buffered playback resumes when a device is available. Long outages can affect synchronization with other players; no claim of uninterrupted multiroom synchronization is made during device changes. The device remains open and produces silence while paused or powered off in Lyrion.

### Launch from the command line

Open the macOS tray app from a source build:

```bash
open "build/Lyrion Player.app"
```

Run the backend directly, replacing the example address with your server:

```bash
./build/player --server 192.0.2.10:3483 --name "My Mac" --mac 02:00:00:00:00:01
```

Omit `--server` for automatic discovery. Reuse the same unique `--mac` value to preserve a standalone player's identity across runs; tray-managed players save their identities automatically. Run `./build/player --help` for supported options.

On Windows, the tray app is `Lyrion Player.exe` and the backend is `player.exe`. Source builds using Visual Studio normally place both under `build/Release/`.

### Troubleshooting

- **No server appears / Discovering.** Make sure Lyrion is running. Try **Manual address** with its host and SlimProto port, normally `3483`. Discovery probes localhost and each active LAN interface, but a VPN can still block LAN access or broadcasts. If a manual address works, use it while investigating the network's discovery filtering.
- **Connecting.** Check that the saved server address and SlimProto port are correct. If a previously selected server changed IP address, select it again or use a stable hostname. Select the player and choose **View Log** for details.
- **Error.** Unexpected exits retry after 1, 2, and 4 seconds. A further failure within a minute leaves the player in Error. Inspect **View Log**, correct the reported problem, then use **Start / Stop** to retry.
- **Connected but silent.** Select this player in Lyrion, start a track, and check player/system volume and the system's selected audio output. Check **View Log** for device or decoding errors. Unsupported formats require server transcoding; see [Playback](#playback).
- **Invalid configuration.** Use **Open configuration folder** to locate `config.json`. The app preserves invalid configuration and reports the error. Back it up before repairing it. To start fresh, quit the app and move the file aside; existing player settings will no longer be loaded.

### Configuration and logs

| Platform | Configuration | Logs |
| --- | --- | --- |
| macOS | `~/Library/Application Support/lyrion-player/config.json` | `~/Library/Logs/lyrion-player/` |
| Windows | `%LOCALAPPDATA%\lyrion-player\config.json` | `%LOCALAPPDATA%\lyrion-player\` |

Logs are named `player-<instance ID>.log`. **View Log** opens the selected player's file. Removing a player retains its logs, and automatic log rotation is not included.

### Remove the app

1. Disable **Start app at login** in the tray menu.
2. Choose **Quit Lyrion Player** to stop its players and exit.
3. On macOS, remove **Lyrion Player.app** from Applications. On Windows, use the installed uninstaller or Settings → Apps. For a portable ZIP installation, remove the folder where you extracted the package.

Configuration and logs remain in the locations above. Keep them to reuse your settings, or delete them after quitting if you want to remove saved data too. On Windows, configuration and logs share a folder.

## Build from source

### Requirements

- Git and CMake 3.24 or later.
- A C++17 compiler and a C99 compiler.
- FLAC, Ogg/Vorbis, mpg123 and soxr development libraries.
- Python 3 for the test suite, enabled by default.
- On macOS, Xcode command-line tools with Objective-C++ and the AppKit SDK.
- On Windows, Visual Studio 2022 with Desktop development with C++ and [vcpkg](https://github.com/microsoft/vcpkg).

### Configure and build

Clone the repository and enter its directory:

```bash
git clone https://github.com/tsengf/lyrion-player.git
cd lyrion-player
```

After installing the dependencies below, configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The tray app is `build/Lyrion Player.app` on macOS or `Lyrion Player.exe` on Windows. The backend is `build/player` on macOS or `player.exe` on Windows. Multi-configuration generators may add a directory such as `Release`.

Development builds use the installed codec libraries. If FLAC is missing, CMake can download and build FLAC 1.5.0; set `-DPLAYER_FETCH_FLAC=OFF` to require an installed library instead.

### macOS dependencies

With [Homebrew](https://brew.sh/):

```bash
brew install cmake flac libvorbis mpg123 libsoxr python
```

With [MacPorts](https://www.macports.org/):

```bash
sudo port selfupdate
sudo port install cmake flac libogg libvorbis mpg123 soxr python313
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/opt/local \
    -DPython3_EXECUTABLE=/opt/local/bin/python3.13
cmake --build build --config Release --parallel
```

These commands assume the default MacPorts prefix, `/opt/local`, with `/opt/local/bin` on `PATH`. The resampler port is named `soxr`. Use separate build directories when switching between Homebrew and MacPorts so cached dependency paths do not carry over.

### Windows dependencies

With `VCPKG_ROOT` set to your vcpkg checkout, run in PowerShell:

```powershell
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
```

The vcpkg manifest installs codec dependencies. Keep the DLLs copied beside `player.exe` with the executable when moving a development build.

### Build options

| Option | Effect | Default |
| --- | --- | --- |
| `PLAYER_BUILD_TESTS` | Build the test suite; turn off to omit the Python dependency. | `ON` |
| `PLAYER_BUILD_TRAY` | Build the menu-bar/system-tray app. | `ON` |
| `PLAYER_BUILD_PACKAGES` | Enable the CMake package target. | Follows `PLAYER_BUILD_TRAY` on a fresh configuration. |
| `PLAYER_AAC` | Enable ADTS/ADIF AAC decoding; requires faad2. | `OFF` |
| `PLAYER_FETCH_FLAC` | Fetch FLAC 1.5.0 if an installed library is unavailable. | `ON` |

To build only the backend, set `-DPLAYER_BUILD_TRAY=OFF -DPLAYER_BUILD_PACKAGES=OFF`. Existing build directories retain cached options; re-enable disabled features explicitly when reconfiguring.

## Build packages

Use the configured build for the platform and architecture you want to distribute. Keep the dependency options used for that build. CPack packages the Tray deployment component, including runtime libraries and licenses, and excludes test executables and the development CLI install.

### Native installers

After building and [running the tests](#tests), create an installer:

```bash
cmake --build build --config Release --target package --parallel
```

With the Visual Studio generator, use uppercase `PACKAGE`:

```powershell
cmake --build build --config Release --target PACKAGE --parallel
```

| Platform | Output example | Packaging requirements |
| --- | --- | --- |
| macOS | `build/LyrionPlayer-0.1.0-macos-arm64.dmg` | `hdiutil` and `codesign`, included with macOS. |
| Windows x64 | `build/LyrionPlayer-0.1.0-windows-x64.exe` | NSIS 3.03 or later on `PATH`. |

The package target builds the app, stages its installation and creates the installer. A normal build does not create a package. Files are written directly to the selected build directory.

macOS DMGs use UDBZ compression and contain the app and an Applications shortcut. Deployment installs codec libraries and licenses inside the app, fixes dependency paths, then signs and verifies the complete bundle. It does not notarize the app. Use dependencies built for your oldest supported OS when distributing broadly.

A universal macOS build requires both architectures in every linked codec library. Configure with `'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'` only when those dependencies are available; the filename then includes `macos-arm64-x86_64`.

### Portable ZIPs

Choose ZIP output in a separate build directory, including your platform's dependency options:

```bash
cmake -S . -B build-zip -DCMAKE_BUILD_TYPE=Release -DCPACK_GENERATOR=ZIP
cmake --build build-zip --config Release --target package --parallel
```

Use uppercase `PACKAGE` with Visual Studio. ZIPs contain the deployed runtime dependencies and do not require NSIS. To create a one-off ZIP from an existing build without changing its configured generator:

```bash
cpack --config build/CPackConfig.cmake -C Release -G ZIP
```

### Staging and CI

To stage a self-contained app or folder without creating an installer:

```bash
cmake --install build --config Release --prefix "$PWD/build/package" --component Tray
```

Fresh macOS builds default to an install prefix of `build/install`; an explicit prefix is preserved. Distribute packages with their licenses and corresponding source.

The [Build and test workflow](https://github.com/tsengf/lyrion-player/actions/workflows/build.yml) uses the same package target. It runs on pushes, pull requests and manual dispatch, verifies the DMG or installs/tests/uninstalls the Windows package, and uploads installers as artifacts for 30 days. It does not publish GitHub Releases automatically.

## Tests

After building:

```bash
ctest --test-dir build -C Release --output-on-failure
```

Tests use local mock servers and a silent audio backend. They cover protocol framing and reconnection, streaming, decoding, resampling, playback timing, volume, fades, track transitions and cancellation. Discovery, managed-player IPC, process lifetime, tray configuration and UI controls are also covered. Tests do not play sound, contact a real music server or modify sign-in registrations.

Physical device switching and real sign-in behavior require manual validation on both platforms. See [development and validation](docs/development.md) for the checks and [architecture](docs/architecture.md) for implementation details.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/player/` | SlimProto, HTTP transport, codecs, buffering and playback. |
| `src/native_audio.c`, `src/native_audio.h` | Native audio output through miniaudio. |
| `src/discovery.c`, `src/discovery.h` | Localhost and LAN server discovery. |
| `src/managed.cpp`, `src/managed.h` | JSON status and managed-player shutdown. |
| `src/tray/` | Shared player manager and native macOS/Windows interfaces. |
| `tests/` | Protocol, playback, discovery, process and tray tests; generated codec fixtures. |
| `CMakeLists.txt` | Application and test build configuration. |
| `cmake/Packaging.cmake`, `cmake/DeployMacOS.cmake` | Installer configuration and macOS deployment. |
| `.github/workflows/build.yml` | Builds, tests and installation packages. |
| `docs/` | Architecture, validation guidance and screenshots. |

## License

[GPL-3.0-or-later](COPYING). See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY.md) for dependency licenses and source information.

The backend has no Squeezelite source or runtime dependency. Miniaudio is used under its MIT No Attribution option. Distribution must also satisfy the codec libraries' licenses and corresponding-source requirements.
