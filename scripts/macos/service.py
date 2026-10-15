#!/usr/bin/env python3
"""Install or remove Player's per-user launchd service. No sudo required."""
import argparse
import os
from pathlib import Path
import plistlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path.home() / ".local/bin/player")
    parser.add_argument("--server", help="Lyrion host[:port]; omit for discovery")
    parser.add_argument("--name", default="Player")
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    label = "org.lyrion.player"
    domain = f"gui/{os.getuid()}"
    target = f"{domain}/{label}"
    plist = Path.home() / "Library/LaunchAgents" / f"{label}.plist"
    if args.uninstall:
        subprocess.run(["launchctl", "bootout", target], check=False)
        plist.unlink(missing_ok=True)
        print("Player service removed. Executable and logs retained.")
        return
    executable = args.executable.expanduser().resolve(strict=True)
    if not os.access(executable, os.X_OK):
        parser.error(f"Not executable: {executable}")
    logs = Path.home() / "Library/Logs/Player"
    logs.mkdir(parents=True, exist_ok=True)
    plist.parent.mkdir(parents=True, exist_ok=True)
    command = [str(executable), "-n", args.name, "-d", "output=info"]
    if args.server:
        command += ["-s", args.server]
    definition = {
        "Label": label,
        "ProgramArguments": command,
        "RunAtLoad": True,
        "KeepAlive": True,
        "ThrottleInterval": 10,
        "ProcessType": "Interactive",
        "StandardOutPath": str(logs / "player.log"),
        "StandardErrorPath": str(logs / "player.log"),
    }
    subprocess.run(["launchctl", "bootout", target], check=False, capture_output=True)
    with plist.open("wb") as output:
        plistlib.dump(definition, output)
    plist.chmod(0o600)
    subprocess.run(["launchctl", "bootstrap", domain, str(plist)], check=True)
    print(f"Player starts now and at sign-in. Logs: {logs / 'player.log'}")


if __name__ == "__main__":
    main()
