# Player

A headless Lyrion Music Server player for macOS and Windows, based on
[Squeezelite](https://github.com/ralph-irving/squeezelite). Control it from Lyrion's
web interface or a compatible controller. Player discovers the server on your
local network, or connects to a specified server.

Automatic discovery probes both localhost (`127.0.0.1`) and the local network's
broadcast address on UDP port 3483. A server on the same Mac or PC can therefore
be found even when network broadcasts are unavailable. The first valid reply
selects the server; use `-s host[:port]` to choose a particular server when more
than one is available or it uses a nonstandard SlimProto port.

Player uses the signed-in user's **system default audio output**. Changing that
output while music is playing reroutes audio through Core Audio on macOS or WASAPI
on Windows. Playback stays connected to Lyrion; it does not restart the player or
the track. A brief gap while the operating system opens the new device is possible.
Shared mode lets other applications play audio too.

## Build

Requires CMake 3.24+, a C compiler, and the FLAC, Ogg/Vorbis, mpg123 and soxr
development libraries. Dependencies must be installed on machines running these
development builds. This repository does not yet produce a self-contained app
installer. If FLAC is missing, CMake can download and build FLAC 1.5.0; set
`-DPLAYER_FETCH_FLAC=OFF` to require installed libraries instead.

### macOS

Install Xcode Command Line Tools, then choose Homebrew or MacPorts for dependencies.
Apple Silicon and Intel builds use the same source.

With [Homebrew](https://brew.sh/):

```sh
brew install cmake flac libvorbis mpg123 libsoxr python
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/player -n "My Mac"
```

With [MacPorts](https://www.macports.org/install.php):

```sh
sudo port selfupdate
sudo port install cmake flac libogg libvorbis mpg123 soxr python313
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/local \
  -DPython3_EXECUTABLE=/opt/local/bin/python3.13
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/player -n "My Mac"
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
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\player.exe -n "My PC"
```

The manifest installs codec dependencies. Keep the DLLs that vcpkg copies beside
`player.exe` with the executable if moving it to another directory.

## Start automatically at sign-in

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

Logs: `~/Library/Logs/Player/player.log`.
Status: `launchctl print gui/$(id -u)/org.lyrion.player`.

### Windows

Keep the executable and codec DLLs in a permanent directory, then register it:

```powershell
.\scripts\windows\Install-Player.ps1 -Executable 'C:\Users\you\AppData\Local\Player\player.exe' -Name 'My PC' -Server '192.168.1.10'
```

Omit `-Server` for discovery. Uninstall automatic startup with:

```powershell
.\scripts\windows\Install-Player.ps1 -Uninstall
```

Logs: `%LOCALAPPDATA%\Player\player.log`. Task Scheduler shows the task as
`LyrionPlayer-<your user SID>`. Windows retries failed runs every minute, up to
999 times per failure sequence. Allow Player through your firewall on your private
network so it can discover/connect to Lyrion.

## Multiple instances

Run one process per server. Give every instance a distinct name and a stable,
unique `-m` identifier. The identifier uses MAC-address notation to identify the
software player to Lyrion; it does not change your network adapter's address.
Without `-m`, instances on the same computer can inherit the same identity.
Use `-s` to select each server explicitly rather than relying on discovery.
Replace the example server IP addresses with your own.

All instances follow the same system default audio output. If more than one
instance plays at once, their audio mixes. Each has its own connection and queue
on its selected server; there is no combined library.

### Run two players manually

On macOS, run these commands in separate terminals:

```sh
./build/player -n "My Mac - Server A" -s 192.168.1.10 -m 02:00:00:00:00:01
./build/player -n "My Mac - Server B" -s 192.168.1.20 -m 02:00:00:00:00:02
```

Stop each instance with Ctrl+C in its terminal. On Windows, launch both from
PowerShell:

```powershell
.\build\Release\player.exe -n 'My PC - Server A' -s 192.168.1.10 -m 02:00:00:00:00:01
.\build\Release\player.exe -n 'My PC - Server B' -s 192.168.1.20 -m 02:00:00:00:00:02
```

### Install multiple macOS startup entries

The bundled `service.py` installer manages one LaunchAgent per user. Running it
twice replaces that entry. To install two instances, create separate LaunchAgents
with unique labels and log files using the example below. Stop any foreground
copies first. If replacing the single-instance service, remove it with the
`--uninstall` command above before installing these entries.

Install the binary to its permanent location:

```sh
cmake --install build --prefix "$HOME/.local"
```

Run the following from a terminal in your signed-in macOS session. For MacPorts,
you can replace `python3` with `/opt/local/bin/python3.13`.

```sh
python3 - <<'PY'
import os
from pathlib import Path
import plistlib
import subprocess

binary = (Path.home() / ".local/bin/player").resolve(strict=True)
agents = Path.home() / "Library/LaunchAgents"
logs = Path.home() / "Library/Logs/Player"
agents.mkdir(parents=True, exist_ok=True)
logs.mkdir(parents=True, exist_ok=True)
domain = f"gui/{os.getuid()}"
instances = [
    ("server-a", "My Mac - Server A", "192.168.1.10", "02:00:00:00:00:01"),
    ("server-b", "My Mac - Server B", "192.168.1.20", "02:00:00:00:00:02"),
]
for instance, name, server, identity in instances:
    label = f"org.lyrion.player.{instance}"
    path = agents / f"{label}.plist"
    definition = {
        "Label": label,
        "ProgramArguments": [str(binary), "-n", name, "-s", server,
                             "-m", identity, "-d", "output=info"],
        "RunAtLoad": True,
        "KeepAlive": True,
        "ThrottleInterval": 10,
        "ProcessType": "Interactive",
        "StandardOutPath": str(logs / f"{instance}.log"),
        "StandardErrorPath": str(logs / f"{instance}.log"),
    }
    subprocess.run(["launchctl", "bootout", f"{domain}/{label}"],
                   check=False, capture_output=True)
    with path.open("wb") as output:
        plistlib.dump(definition, output)
    path.chmod(0o600)
    subprocess.run(["launchctl", "bootstrap", domain, str(path)], check=True)
    print(f"Started {label}; log: {logs / (instance + '.log')}")
PY
```

Both instances start immediately and at subsequent sign-ins. Check an instance:

```sh
launchctl print "gui/$(id -u)/org.lyrion.player.server-a"
tail -f "$HOME/Library/Logs/Player/server-a.log"
```

To stop and uninstall both startup entries, retaining the binary and logs:

```sh
for instance in server-a server-b; do
  launchctl bootout "gui/$(id -u)/org.lyrion.player.$instance"
  rm -f "$HOME/Library/LaunchAgents/org.lyrion.player.$instance.plist"
done
```

### Install multiple Windows startup entries

The bundled `Install-Player.ps1` manages one task per user. Running it twice
replaces that task. To install multiple instances, register separate Task
Scheduler tasks as below. Close foreground players first. If replacing the
single-instance task, remove it using `Install-Player.ps1 -Uninstall` above.

Keep `player.exe` and its codec DLLs together in a permanent directory. Run this
in PowerShell as the user who will play music, adjusting the executable path and
server addresses:

```powershell
$binary = (Resolve-Path "$env:LOCALAPPDATA\Player\player.exe").Path
$user = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$logs = Join-Path $env:LOCALAPPDATA 'Player'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$instances = @(
    @{ Id = 'server-a'; Name = 'My PC - Server A'; Server = '192.168.1.10'; Mac = '02:00:00:00:00:01' },
    @{ Id = 'server-b'; Name = 'My PC - Server B'; Server = '192.168.1.20'; Mac = '02:00:00:00:00:02' }
)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $user.Name
$principal = New-ScheduledTaskPrincipal -UserId $user.Name -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew -StartWhenAvailable
foreach ($instance in $instances) {
    $taskName = "LyrionPlayer-$($user.User.Value)-$($instance.Id)"
    $log = Join-Path $logs "$($instance.Id).log"
    $arguments = '-n "{0}" -s {1} -m {2} -d output=info -f "{3}"' -f `
        $instance.Name, $instance.Server, $instance.Mac, $log
    $action = New-ScheduledTaskAction -Execute $binary -Argument $arguments `
        -WorkingDirectory (Split-Path $binary)
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName
    }
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
        -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName $taskName
    Write-Output "Started $taskName; log: $log"
}
```

Use names without embedded double quotes in this example. Both tasks start
immediately and at subsequent sign-ins. Logs are
`%LOCALAPPDATA%\Player\server-a.log` and `server-b.log`. View each task in Task
Scheduler, or list them with:

```powershell
Get-ScheduledTask -TaskName "LyrionPlayer-$([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value)-*"
```

To stop and uninstall these two tasks, retaining the executable and logs:

```powershell
$sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
foreach ($instance in 'server-a', 'server-b') {
    $taskName = "LyrionPlayer-$sid-$instance"
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
}
```

These manually created entries are independent of the single-instance installers.
Use the cleanup commands in this section to remove them.

## Playback and options

```sh
player -n "Office"                      # Discover Lyrion
player -n "Office" -s 192.168.1.10       # Connect directly
player -n "Office" -d output=info       # Log device recovery and rerouting
player -l                               # Show the default-output policy
```

Player inherits Squeezelite's command-line parser. The audio adapter requires
`default`; omit `-o`, `-a`, and `-r`. Fixed-device, exclusive-mode, and hardware
format controls from upstream do not apply. Use `-m 02:xx:xx:xx:xx:xx` with a
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
Lyrion. Automatic log rotation and packaged installers are not included yet.

## Validation

`ctest` uses a separate `player_test_audio` executable with a silent backend. It
tests a SlimProto handshake against a local mock server, HTTP PCM streaming,
44.1-to-48 kHz resampling, an advancing playback clock, pause, and rejection of
fixed-device output. It also tests localhost discovery against an isolated mock
server without `-s`, including rejection of invalid discovery replies. Discovery
tests use a temporary port through a test-only override; production always probes
port 3483. Tests never play sound or connect to your real music server.
A CI workflow is provided to run these checks on macOS and Windows.

Physical device switching requires manual validation on both systems:

1. Start a track in Lyrion; change the system output between speakers and a
   USB/Bluetooth device. Confirm audio moves and the same track remains active.
2. Switch while paused, then resume; confirm output uses the new default.
3. Disconnect the active output; reconnect it or select another output. Confirm
   recovery without restarting Player.
4. Switch between devices configured for different sample rates, skip tracks,
   adjust volume, and test sleep/wake.
5. Install startup, sign out/in, verify one player appears, then uninstall startup.

Hardware rerouting and service installation have not been verified by the silent
tests. See [architecture](docs/architecture.md) for the ownership and recovery model.

## License and upstream

Player is GPL-3.0-or-later because it incorporates Squeezelite. Original copyright
notices are retained; the full GPL text is in [COPYING](COPYING).
See [third-party notices](THIRD_PARTY.md)
for pinned source revisions and local modifications. Miniaudio is used under its
MIT No Attribution option. Redistributing a build also requires satisfying its
codec libraries' licenses and supplying corresponding GPL source as applicable.
