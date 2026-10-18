# Lyrion Player

A Lyrion Music Server player for macOS and Windows, based on
[Squeezelite](https://github.com/ralph-irving/squeezelite). Control it from Lyrion's
web interface or a compatible controller. Native macOS menu-bar and Windows
system-tray apps manage player instances without command-line configuration.
Player discovers the server on your
local network, or connects to a specified server.

Automatic discovery probes localhost (`127.0.0.1`), the general broadcast address,
and each active IPv4 LAN interface's subnet broadcast on UDP port 3483. LAN probes
use separate sockets bound to their interface's local address; on macOS they are
also pinned to that interface. For example, a Wi-Fi address of `192.168.1.144/24`
produces a probe to `192.168.1.255`. Interfaces are refreshed on each retry;
loopback and point-to-point VPN interfaces are excluded from the LAN list.
A server on the same Mac or PC can be found even without LAN access. The first valid reply
selects the server; use `--server host[:port]` to choose a particular server when more
than one is available or it uses a nonstandard SlimProto port.

This improves discovery when a VPN changes the default broadcast route. It cannot
override VPN policies that block LAN traffic. Use `--debug slimproto=debug` to see
the interface and destination of each discovery probe. If direct access works
but broadcasts remain filtered, specify the server with `--server`.

Player uses the signed-in user's **system default audio output**. Changing that
output while music is playing reroutes audio through Core Audio on macOS or WASAPI
on Windows. Playback stays connected to Lyrion; it does not restart the player or
the track. A brief gap while the operating system opens the new device is possible.
Shared mode lets other applications play audio too.

## Tray app

Requires macOS 13 or later (Apple Silicon or Intel), or Windows 10 or later (x64).
Build the app using the instructions below. Packaged builds include the backend
and codec libraries; users do not need Python, Homebrew, MacPorts, or vcpkg.

1. On macOS, copy **Lyrion Player.app** from the package into Applications and
   open it. On Windows, extract the entire package into a permanent folder and
   open **Lyrion Player.exe**, keeping `player.exe` and all DLLs beside it.
2. Open **Settings…** from the menu-bar/tray icon, then choose **Add…**. Enter a
   player name and select **Automatic**, a discovered server, or **Manual address**.
   The server list refreshes automatically at startup and 15 seconds after each
   scan completes. Manual addresses use a separate SlimProto port, normally `3483`.
   Automatic immediately displays the known server address in the read-only host
   field, and updates it when discovery completes. Saving keeps discovery enabled.
   Hover over the info icon beside each setting for a description.
3. Save, select the instance, and choose **Start / Stop**. Select this player in
   Lyrion and start music there. Repeat Add for additional instances/servers.
4. Enable **Start at login** in the tray menu to register the tray app once.
   It is off initially.
   **Start this instance with the app** controls which instances launch when the
   tray app opens, and is on for new instances.

**Only the tray app is registered at sign-in.** It owns the player child
processes; instances have no individual LaunchAgents, scheduled tasks, or Windows
services. Closing the Settings window keeps playback running. **Quit Lyrion
Player** stops every managed player before exiting. Sign-in startup runs in your
user session so playback follows the selected output device.

The tray menu provides Settings, a checked Start at login option, Start All,
Stop All, and Quit. Individual players and their connection/audio status appear
only in Settings. Select a player there to Start/Stop, Edit, View Log, or Open
Lyrion. These actions are greyed out when no player is selected. The read-only
Auto discovery column shows Yes for automatic instances; Server shows their
discovered IP and SlimProto port once known and stays blank until discovery
finds a server. Manual instances show No and their configured host and port.
Start with app shows Yes or No according to the instance’s saved startup
setting. Editing a running instance restarts only that instance. Unexpected
player exits retry after 1, 2 and 4 seconds; another failure within a minute
leaves that instance in Error until you start it again. Closing or crashing the
tray app cannot leave its players running independently.

Each instance gets a saved, unique MAC-style **Player ID**. Keep it unchanged to
preserve Lyrion's identity and settings. Use a different ID for every instance.
Automatic selects the first replying server; choose a discovered server or manual
address to keep an instance attached to a particular server. Discovered selections
save the server's current IP address. Use a stable hostname/manual address or
select it again if that address changes. Open Lyrion uses the discovered web port
(default `9000`); set the optional Web URL for a custom HTTPS/proxy address.

On macOS, Start at login uses `SMAppService`. If approval is required, enable
Lyrion Player in System Settings → General → Login Items. On Windows, it uses one
`LyrionPlayerTray` value in your own account's Run registry key. Put the app in its
permanent location before enabling startup. These are user-session registrations;
no administrator privileges are needed.

Configuration:

- macOS: `~/Library/Application Support/lyrion-player/config.json`.
- Windows: `%LOCALAPPDATA%\lyrion-player\config.json`.

Logs use `player-<instance ID>.log` in `~/Library/Logs/lyrion-player/` on macOS and
`%LOCALAPPDATA%\lyrion-player\` on Windows. Use View Log to open the selected
instance's file. Removing an instance retains its logs. Invalid configuration is
preserved and reported; Open configuration folder lets you locate it for repair.

## Build

Requires CMake 3.24+, C/C++ compilers (Objective-C++ on macOS), and the FLAC, Ogg/Vorbis, mpg123 and soxr
development libraries. Dependencies must be installed on machines running these
development builds. The Tray packaging step below copies runtime libraries
into a self-contained app/folder. If FLAC is missing, CMake can download and build FLAC 1.5.0; set
`-DPLAYER_FETCH_FLAC=OFF` to require installed libraries instead.

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
`/opt/local/bin` is on your `PATH`. Python is selected explicitly for the tests;
you can also use `/opt/local/bin/python3.13` for the startup script below without
changing your system's default Python. Use a fresh build directory when switching
between Homebrew and MacPorts so CMake does not reuse cached dependency paths.

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

Omit `-DPLAYER_BUILD_TRAY=ON` for the original headless build. Python is only needed
for tests and the optional legacy macOS registration script.

## Command-line startup (legacy alternative)

The following scripts remain available for headless use. They create individual
startup entries and are independent of the tray app. For tray-managed instances,
use the app's Start at login setting. Remove any old script-created startup
entries before adding the same players in Settings to avoid duplicate players.


The background player runs in your own user session so it follows your audio
selection. It starts when you sign in, restarts after a crash, and stops when you
sign out. On macOS this is a launchd LaunchAgent. On Windows it is a per-user
Task Scheduler task using an interactive token, rather than a machine-wide SCM
service. Neither installation requires administrator privileges.

The scripts start playback immediately after installation; they do not change the
system's default device. Stop any foreground copy first to avoid duplicate players.

### macOS

Install to a permanent location before registering:

```sh
cmake --install build --prefix "$HOME/.local"
python3 scripts/macos/service.py --name "My Mac" --server 192.168.1.10
```

Omit `--server` for discovery. Use `--executable /absolute/path/to/player` to select
another location. Uninstall automatic startup with:

```sh
python3 scripts/macos/service.py --uninstall
```

Logs: `~/Library/Logs/lyrion-player/player.log`.
Status: `launchctl print gui/$(id -u)/org.lyrion.player`.

### Windows

Keep the executable and codec DLLs in a permanent directory, then register it:

```powershell
.\scripts\windows\service.ps1 -Executable 'C:\Users\you\AppData\Local\Player\player.exe' -Name 'My PC' -Server '192.168.1.10'
```

Omit `-Server` for discovery. Uninstall automatic startup with:

```powershell
.\scripts\windows\service.ps1 -Uninstall
```

Logs: `%LOCALAPPDATA%\lyrion-player\player.log`. Task Scheduler shows the task as
`LyrionPlayer-<your user SID>`. Windows retries failed runs every minute, up to
999 times per failure sequence. Allow Player through your firewall on your private
network so it can discover/connect to Lyrion.

## Multiple instances

In the tray app, use Add once per server; identities and child processes are managed
automatically. All instances follow the same default output, and simultaneous audio
mixes. The following examples are for manual/headless use.

Run one process per server. Give every instance a distinct name and a stable,
unique `--mac` identifier. The identifier uses MAC-address notation to identify the
software player to Lyrion; it does not change your network adapter's address.
Without `--mac`, instances on the same computer can inherit the same identity.
Use `--server` to select each server explicitly rather than relying on discovery.
Replace the example server IP addresses with your own.

All instances follow the same system default audio output. If more than one
instance plays at once, their audio mixes. Each has its own connection and queue
on its selected server; there is no combined library.

### Run two players manually

On macOS, run these commands in separate terminals:

```sh
./build/player --name "My Mac - Server A" --server 192.168.1.10 --mac 02:00:00:00:00:01
./build/player --name "My Mac - Server B" --server 192.168.1.20 --mac 02:00:00:00:00:02
```

Stop each instance with Ctrl+C in its terminal. On Windows, launch both from
PowerShell:

```powershell
.\build\Release\player.exe --name 'My PC - Server A' --server 192.168.1.10 --mac 02:00:00:00:00:01
.\build\Release\player.exe --name 'My PC - Server B' --server 192.168.1.20 --mac 02:00:00:00:00:02
```

### Install multiple macOS startup entries

Use `service.py` once for each instance. `--instance` selects a separate startup
entry, and `--mac` supplies its stable player identity. Named instances require
`--mac`; give each instance a different identifier. Replace the example server
addresses and names before running these commands. Stop any foreground copies
first. No `sudo` is needed.

Install the binary to a permanent location:

```sh
cmake --install build --prefix "$HOME/.local"
```

If replacing a previously installed default service, remove it first:

```sh
python3 scripts/macos/service.py --uninstall
```

Install and start Server A's player:

```sh
python3 scripts/macos/service.py --instance server-a \
  --name "My Mac - Server A" --server 192.168.1.10 --mac 02:00:00:00:00:01
```

Install and start Server B's player separately:

```sh
python3 scripts/macos/service.py --instance server-b \
  --name "My Mac - Server B" --server 192.168.1.20 --mac 02:00:00:00:00:02
```

For MacPorts, you can replace `python3` with `/opt/local/bin/python3.13`.
Use `--executable /absolute/path/to/player` if the binary is elsewhere. Both
instances start immediately and at subsequent sign-ins. Running an install
command again replaces and restarts only that named instance; supply its full
configuration again, including the same `--mac` identifier.

The LaunchAgents are `org.lyrion.player.server-a` and
`org.lyrion.player.server-b`. Check each instance's status:

```sh
launchctl print "gui/$(id -u)/org.lyrion.player.server-a"
launchctl print "gui/$(id -u)/org.lyrion.player.server-b"
```

Each instance has its own log. Ctrl+C stops following the logs without stopping
the players:

```sh
tail -f "$HOME/Library/Logs/lyrion-player/player-server-a.log" "$HOME/Library/Logs/lyrion-player/player-server-b.log"
```

Stop and uninstall Server A's startup entry, retaining the binary and logs:

```sh
python3 scripts/macos/service.py --instance server-a --uninstall
```

Stop and uninstall Server B's startup entry separately:

```sh
python3 scripts/macos/service.py --instance server-b --uninstall
```

Omitting `--instance` continues to manage the original `org.lyrion.player` service
and `player.log`; it does not update or uninstall any named instance.

### Install multiple Windows startup entries

Use `service.ps1` once for each instance, just like `service.py` on macOS.
`-Instance` selects a separate startup task, and `-Mac` supplies its stable player
identity. Named instances require `-Mac`; give every instance a different
identifier. Replace the example server addresses and names before running the
commands. Close any foreground copies first.

Keep `player.exe` and its codec DLLs together in a permanent directory. The
commands below use the default location, `%LOCALAPPDATA%\Player\player.exe`.
If your executable is elsewhere, add `-Executable 'C:\path\to\player.exe'` to
each install command. Run PowerShell as the user who will play music; no
administrator privileges are needed.

If replacing a previously installed default task, remove it first:

```powershell
.\scripts\windows\service.ps1 -Uninstall
```

Install and start Server A's player:

```powershell
.\scripts\windows\service.ps1 -Instance server-a `
  -Name 'My PC - Server A' -Server 192.168.1.10 -Mac 02:00:00:00:00:01
```

Install and start Server B's player separately:

```powershell
.\scripts\windows\service.ps1 -Instance server-b `
  -Name 'My PC - Server B' -Server 192.168.1.20 -Mac 02:00:00:00:00:02
```

Both instances start immediately and at subsequent sign-ins. Running an install
command again replaces and restarts only that named instance; supply its full
configuration again, including the same `-Mac` identifier.

Task Scheduler shows the entries as `LyrionPlayer-<your user SID>-server-a` and
`LyrionPlayer-<your user SID>-server-b`. List their status from PowerShell:

```powershell
Get-ScheduledTask -TaskName "LyrionPlayer-$([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value)-*"
```

Each instance has its own log. Read either log with:

```powershell
Get-Content "$env:LOCALAPPDATA\lyrion-player\player-server-a.log" -Tail 30
Get-Content "$env:LOCALAPPDATA\lyrion-player\player-server-b.log" -Tail 30
```

Stop and uninstall Server A's startup entry, retaining the executable and logs:

```powershell
.\scripts\windows\service.ps1 -Instance server-a -Uninstall
```

Stop and uninstall Server B's startup entry separately:

```powershell
.\scripts\windows\service.ps1 -Instance server-b -Uninstall
```

Omitting `-Instance` continues to manage the original `LyrionPlayer-<your user SID>`
task and `player.log`; it does not update or uninstall any named instance.

## Playback and options

```sh
player --name "Office"                          # Discover Lyrion
player --name "Office" --server 192.168.1.10      # Connect directly
player --name "Office" --debug output=info       # Log device recovery and rerouting
player --list-devices                           # Show the default-output policy
```

Player supports long option names in the examples above and retains Squeezelite's
short flags for compatibility. Use `player --help` to see available options.
CMake and macOS system commands retain short flags where no long form exists.
The audio adapter requires `default`; omit `--output`, `--audio-params`, and
`--rates`. Fixed-device, exclusive-mode, and hardware
format controls from upstream do not apply. Use `--mac 02:xx:xx:xx:xx:xx` with a
unique locally administered MAC-style identifier if you need a stable player
identity independent of network interfaces. Do not run two copies with the same
identifier against one server.

Native decoding: FLAC, MP3, Ogg Vorbis, and PCM (including WAV/AIFF). Other formats
require Lyrion transcoding. Optional AAC support requires faad2 and
`-DPLAYER_AAC=ON`. Direct HTTPS streams are not enabled in this initial build;
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
Lyrion. Automatic log rotation and installer wizards are not included yet.

## Validation

`ctest` uses a separate `player_test_audio` executable with a silent backend. It
tests a SlimProto handshake against a local mock server, HTTP PCM streaming,
44.1-to-48 kHz resampling, an advancing playback clock, pause, and rejection of
fixed-device output. It also tests localhost discovery against an isolated mock
server without `--server`, including rejection of invalid discovery replies. Discovery
tests include replies received only on a separate source-bound interface socket.
They use a temporary port and simulated interface through test-only overrides;
production always probes
port 3483. Tests never play sound or connect to your real music server.
A separate service test checks that named macOS entries can be installed,
updated, and removed independently while keeping the original default entry
compatible. It writes only to a temporary directory and mocks `launchctl`;
it does not install or start services on your machine.
On Windows, an additional PowerShell test checks independent task installation,
updates, removal, and argument validation using in-memory substitutes for Task
Scheduler and filesystem operations.
Broadcast calculation tests also cover different subnet sizes and reject masks
without a usable subnet broadcast. For a manual UDP-only check against real
servers, run `./build/player_discovery_test --probe`; it logs the interface probes
and first replying server without registering a player or playing audio.
Managed-mode tests verify JSON status, graceful stop, stdin EOF, and connected
shutdown against mock servers. Shared tray tests cover configuration, identities,
independent lifecycle, bounded restarts and migration failures. Native process tests
exercise real inherited pipes, independent stop and force termination with the
silent backend. Native UI smoke tests create the settings/editor controls using a
temporary profile. Metadata discovery tests collect multiple mock replies. None of
these tests modify sign-in registrations or import real user services.
A CI workflow builds/tests/packages the native apps on macOS and Windows.

Physical device switching requires manual validation on both systems:

1. Start a track in Lyrion; change the system output between speakers and a
   USB/Bluetooth device. Confirm audio moves and the same track remains active.
2. Switch while paused, then resume; confirm output uses the new default.
3. Disconnect the active output; reconnect it or select another output. Confirm
   recovery without restarting Player.
4. Switch between devices configured for different sample rates, skip tracks,
   adjust volume, and test sleep/wake.
5. Install startup, sign out/in, verify one player appears, then uninstall startup.

Hardware rerouting, real sign-in registration, and migration of real OS entries
require manual verification on each target system. See [architecture](docs/architecture.md) for the ownership and recovery model.

## License and upstream

Player is GPL-3.0-or-later because it incorporates Squeezelite. Original copyright
notices are retained; the full GPL text is in [COPYING](COPYING).
See [third-party notices](THIRD_PARTY.md)
for pinned source revisions and local modifications. Miniaudio is used under its
MIT No Attribution option. Redistributing a build also requires satisfying its
codec libraries' licenses and supplying corresponding GPL source as applicable.
