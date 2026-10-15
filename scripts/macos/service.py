#!/usr/bin/env python3
"""Install or remove Player's per-user launchd service. No sudo required."""
import argparse
import os
from pathlib import Path
import plistlib
import re
import subprocess


def instance_name(value):
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", value):
        raise argparse.ArgumentTypeError(
            "use 1–64 lowercase letters, digits, hyphens or underscores; start with a letter or digit")
    return value


def player_mac(value):
    if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", value):
        raise argparse.ArgumentTypeError("use six hexadecimal octets, e.g. 02:00:00:00:00:01")
    octets = bytes.fromhex(value.replace(":", ""))
    if not any(octets) or octets[0] & 1:
        raise argparse.ArgumentTypeError("use a nonzero unicast player identifier")
    return value.lower()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path.home() / ".local/bin/player")
    parser.add_argument("--server", help="Lyrion host[:port]; omit for discovery")
    parser.add_argument("--name", default="Player")
    parser.add_argument("--instance", type=instance_name,
                        help="Named startup entry, e.g. server-a; omit for the original default service")
    parser.add_argument("--mac", type=player_mac,
                        help="Stable player identifier passed as -m; required when installing a named instance")
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args(argv)
    if args.instance and not args.uninstall and not args.mac:
        parser.error("--mac is required with --instance; assign a unique identifier to each player")
    label = "org.lyrion.player" + (f".{args.instance}" if args.instance else "")
    domain = f"gui/{os.getuid()}"
    target = f"{domain}/{label}"
    plist = Path.home() / "Library/LaunchAgents" / f"{label}.plist"
    if args.uninstall:
        subprocess.run(["launchctl", "bootout", target], check=False)
        plist.unlink(missing_ok=True)
        print(f"Removed {label}. Executable and logs retained.")
        return
    executable = args.executable.expanduser().resolve(strict=True)
    if not os.access(executable, os.X_OK):
        parser.error(f"Not executable: {executable}")
    logs = Path.home() / "Library/Logs/lyrion-player"
    logs.mkdir(parents=True, exist_ok=True)
    log_file = logs / (f"player-{args.instance}.log" if args.instance else "player.log")
    plist.parent.mkdir(parents=True, exist_ok=True)
    command = [str(executable), "-n", args.name, "-d", "output=info"]
    if args.server:
        command += ["-s", args.server]
    if args.mac:
        command += ["-m", args.mac]
    definition = {
        "Label": label,
        "ProgramArguments": command,
        "RunAtLoad": True,
        "KeepAlive": True,
        "ThrottleInterval": 10,
        "ProcessType": "Interactive",
        "StandardOutPath": str(log_file),
        "StandardErrorPath": str(log_file),
    }
    subprocess.run(["launchctl", "bootout", target], check=False, capture_output=True)
    with plist.open("wb") as output:
        plistlib.dump(definition, output)
    plist.chmod(0o600)
    subprocess.run(["launchctl", "bootstrap", domain, str(plist)], check=True)
    print(f"{label} starts now and at sign-in. Logs: {log_file}")


if __name__ == "__main__":
    main()
