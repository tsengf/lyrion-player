# Lyrion Player

An independent Lyrion Music Server player for macOS and Windows. Create and
manage players from the macOS menu bar or Windows system tray, then control playback from
Lyrion's web interface or a compatible controller.

The player discovers servers on your computer and local network, or connects to a
server you choose. Audio follows the signed-in user's **system default output**,
including changes made while music is playing. Switching devices keeps the same
track and server connection; a brief gap while the new device opens is possible.
Shared audio lets other applications play sound too.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/settings-macos-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/settings-macos.png">
  <img alt="Lyrion Player Settings on macOS" src="docs/images/settings-macos.png">
</picture>

*Settings on macOS, shown in light or dark mode to match your theme. Player names
and IP addresses are fictional examples.*

## Install

The app targets macOS 13 or later (Apple Silicon or Intel) and Windows 10 or later
(x64). A package's codec libraries can require a newer OS, so the app's deployment
target alone does not guarantee that every package runs on those minimum versions.

Download a package from a successful run of the
[Build and test workflow](https://github.com/tsengf/lyrion-player/actions/workflows/build.yml).
Choose `lyrion-player-macos-arm64` for Apple Silicon,
`lyrion-player-macos-x86_64` for Intel Macs, or `lyrion-player-windows-x64` for
Windows. Extract the downloaded artifact and its packaged ZIP as needed. You can
also [build from source](#build).

- **macOS:** Copy **Lyrion Player.app** into Applications and open it.
- **Windows:** Extract the entire package into a permanent folder and open
  **Lyrion Player.exe**. Keep `player.exe`, the DLLs, and licenses beside it.

Packages include the backend and codec libraries; running them does not require
Python, Homebrew, MacPorts, or vcpkg. These are development distributions:
macOS Developer ID signing/notarization and Windows Authenticode signing are not
configured, so the operating system may show an unverified-app warning.

## Quick start

1. Open **Settings…** from the menu-bar/tray icon and choose **Add…**.
2. Enter a player name. Choose **Automatic** to discover a server, a discovered
   server to select it explicitly, or **Manual address** to enter a host and
   SlimProto port (normally `3483`). Hover over each info icon for help.
3. Save, select the player, and choose **Start / Stop**. In Lyrion, select that
   player and start music. Repeat **Add…** for additional players or servers.
4. Optionally enable **Start app at login** in the tray menu. This is off initially.
   The separate **Start this instance with the app** setting is on for new players.

Closing Settings keeps playback running. **Quit Lyrion Player** stops all players
managed by the app before exiting.

## Settings

Settings opens on the monitor where you click the tray icon. It opens at its
minimum size, and the table columns fit the window as you resize it. Player status
appears here rather than in the tray menu.

| Column | Meaning |
| --- | --- |
| Player | The name displayed in Lyrion. |
| Auto discovery | Yes for Automatic; No for a selected server or manual address. |
| Server | The discovered IP address and SlimProto port, or the configured host and port. Blank until automatic discovery finds a server. |
| Start with app | Yes if the player starts when the tray app opens; otherwise No. |
| Status | The player's current connection and audio status. |

Select a player to **Edit**, **Remove**, **Start / Stop**, **View Log**, or
**Open Lyrion**. Actions requiring a selection are greyed out until you select one.
Editing a running player restarts that player.

### Server selection

The server list scans automatically at startup and again 15 seconds after each
scan completes. **Automatic** selects the first valid server reply. Its read-only
host field displays the known server address immediately and updates when
discovery completes; saving keeps discovery enabled. The address preview does not
pin the player to that server.

Choose a discovered server or **Manual address** to stay attached to a particular
server. A discovered selection saves the server's current IP address. If that
address changes, select it again or use a stable hostname with Manual address.

**Open Lyrion** uses the discovered web port (default `9000`). Set the optional
**Web URL** for a custom HTTPS or proxy address. The web port and SlimProto port
serve different purposes; enter the SlimProto port in the server port field.

### Multiple players

Each player connects to one Lyrion server at a time, with its own playback queue.
Create separate players to connect to multiple servers. All players use the same
system default output; simultaneous playback mixes together.

Player names may repeat, but distinct names make them easier to identify. Each
player receives a saved, unique MAC-style **Player ID**. Keep that ID unchanged to
preserve its identity and settings in Lyrion, and use a different ID for every
player. The tray app manages only the players it launches; it does not adopt
players started separately.

### Startup and tray menu

| Setting | Effect | Default |
| --- | --- | --- |
| Start app at login | Opens the tray app when you sign in. | Off |
| Start this instance with the app | Starts that player whenever the tray app opens. | On for new players |

**Only the tray app is registered at login.** Players have no individual
LaunchAgents, scheduled tasks, or Windows services. Startup runs in your user
session so playback follows the selected output device. Put the app in its
permanent location before enabling startup; administrator privileges are not
needed.

If macOS requests approval, enable Lyrion Player in System Settings → General →
Login Items. See [architecture](docs/architecture.md) for registration and process
ownership details.

The tray menu contains **Settings…**, the **Start app at login** toggle,
**Start all players**, **Stop all players**, **About Lyrion Player**, and
**Quit Lyrion Player**. About shows the version, credits, license, and project
website.

## Troubleshooting

- **No server appears / Discovering:** Make sure Lyrion is running. Try
  **Manual address** with its host and SlimProto port, normally `3483`. Discovery
  probes localhost and each active LAN interface, but a VPN can still block LAN
  access or broadcasts. If a manual address works, use it while investigating the
  network's discovery filtering.
- **Connecting:** Check that the saved server address and SlimProto port are
  correct. If a previously selected server changed IP address, select it again or
  use a stable hostname. Select the player and choose **View Log** for details.
- **Error:** Unexpected exits retry after 1, 2, and 4 seconds. A further failure
  within a minute leaves the player in Error. Inspect **View Log**, correct the
  reported problem, then use **Start / Stop** to retry.
- **Connected but silent:** Select this player in Lyrion, start a track, and check
  player/system volume and the system's selected audio output. Check **View Log**
  for device or decoding errors. Unsupported formats require server transcoding;
  see [Playback](#playback).
- **Invalid configuration:** Use **Open configuration folder** to locate
  `config.json`. The app preserves invalid configuration and reports the error.
  Back it up before repairing it. To start fresh, quit the app and move the file
  aside; existing player settings will no longer be loaded.

### Configuration and logs

| Platform | Configuration | Logs |
| --- | --- | --- |
| macOS | `~/Library/Application Support/lyrion-player/config.json` | `~/Library/Logs/lyrion-player/` |
| Windows | `%LOCALAPPDATA%\lyrion-player\config.json` | `%LOCALAPPDATA%\lyrion-player\` |

Logs are named `player-<instance ID>.log`. **View Log** opens the selected player's
file. Removing a player retains its logs, and automatic log rotation is not
included.

## Remove the app

1. Disable **Start app at login** in the tray menu.
2. Choose **Quit Lyrion Player** to stop its players and exit.
3. On macOS, remove **Lyrion Player.app** from Applications. On Windows, remove
   the folder where you extracted the package.

Configuration and logs remain in the locations above. Keep them to reuse your
settings, or delete them after quitting if you want to remove saved data too.
On Windows, configuration and logs share a folder.

## Playback

Native decoding: FLAC, MP3, Ogg Vorbis, and PCM (including WAV/AIFF). Other formats
require Lyrion transcoding. Optional ADTS/ADIF AAC support requires faad2 and
`-DPLAYER_AAC=ON`; MP4 containers require server transcoding. Direct HTTPS
streams are not enabled in this initial build;
use the server's HTTP stream/proxy path. This is a stereo PCM player, with no DSD
passthrough or bit-perfect/exclusive output.

Decoded music is resampled using soxr to a stable 48 kHz, signed 32-bit stereo
stream. Miniaudio adapts that to the selected device's native format. Keeping a
stable software clock avoids changing Lyrion's stream capabilities when switching
between devices with different sample rates. Resampling defaults to soxr HQ.

If no usable device exists, Player remains connected to Lyrion and retries opening
the default output every two seconds. Buffered playback resumes when a device is
available. Long outages can affect synchronization with other players; no claim
of uninterrupted multiroom synchronization is made during device changes.
The device remains open and produces silence while paused or powered off in
Lyrion. Installer wizards are not included yet.

## Build

Requires Git, CMake 3.24+, C/C++ compilers (Objective-C++ on macOS), and the
FLAC, Ogg/Vorbis, mpg123 and soxr development libraries. Dependencies must be installed on machines running these
development builds. The Tray packaging step below copies runtime libraries
into a self-contained app/folder. If FLAC is missing, CMake can download and
build FLAC 1.5.0; set
`-DPLAYER_FETCH_FLAC=OFF` to require installed libraries instead.

First clone the repository and enter its directory. These commands work in a
macOS shell or Windows PowerShell:

```sh
git clone https://github.com/tsengf/lyrion-player.git
cd lyrion-player
```

### macOS

Install Xcode Command Line Tools, then choose Homebrew or MacPorts for dependencies.
Apple Silicon and Intel builds use the same source.

With [Homebrew](https://brew.sh/):

```sh
brew install cmake flac libvorbis mpg123 libsoxr python
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPLAYER_BUILD_TRAY=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open "build/Lyrion Player.app"
```

With [MacPorts](https://www.macports.org/install.php):

```sh
sudo port selfupdate
sudo port install cmake flac libogg libvorbis mpg123 soxr python313
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPLAYER_BUILD_TRAY=ON \
  -DCMAKE_PREFIX_PATH=/opt/local \
  -DPython3_EXECUTABLE=/opt/local/bin/python3.13
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open "build/Lyrion Player.app"
```

The MacPorts resampler port is named [`soxr`](https://ports.macports.org/port/soxr/).
The commands assume the default MacPorts prefix, `/opt/local`, and that
`/opt/local/bin` is on your `PATH`. Python is selected explicitly for the tests.
Use a fresh build directory when switching between Homebrew and MacPorts so CMake
does not reuse cached dependency paths.

### Windows

Use Visual Studio 2022 with Desktop development with C++, CMake, Python 3, and
[vcpkg](https://github.com/microsoft/vcpkg). In PowerShell, with `VCPKG_ROOT` set to
your vcpkg checkout:

```powershell
cmake -S . -B build -A x64 -DPLAYER_BUILD_TRAY=ON "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
& ".\build\Release\Lyrion Player.exe"
```

The manifest installs codec dependencies. Keep the DLLs that vcpkg copies beside
`player.exe` with the executable if moving it to another directory.

### Create a self-contained package

On macOS:

```sh
cmake --install build --prefix "$PWD/build/package" --component Tray
open "build/package/Lyrion Player.app"
```

On Windows:

```powershell
cmake --install build --config Release --prefix "$PWD/build/package" --component Tray
& ".\build\package\Lyrion Player.exe"
```

Distribute the whole package with its licenses and corresponding source. The macOS
step bundles dylibs, fixes their paths and applies an ad-hoc signature. Developer
ID signing/notarization and Windows Authenticode signing require release credentials
and are not configured. These are unsigned development distributions for OS trust
purposes. CI builds separate macOS ARM64/Intel and Windows x64 ZIP artifacts.
Dependencies can set a higher minimum OS version than the app itself; use dependency
builds targeting your oldest supported OS when distributing broadly.

The player backend and tray manager use C++17; native audio and discovery use C99.
Python is only needed for tests. See [development and validation](docs/development.md)
for test coverage and hardware checks.

## License and dependencies

Player is GPL-3.0-or-later; the full GPL text is in [COPYING](COPYING).
The backend implements SlimProto, HTTP transport, decoding, buffering, and playback
without Squeezelite source or runtime dependencies. See
[third-party notices](THIRD_PARTY.md) for the remaining dependencies.
Miniaudio is used under its
MIT No Attribution option. Redistributing a build also requires satisfying its
codec libraries' licenses and supplying corresponding GPL source as applicable.
