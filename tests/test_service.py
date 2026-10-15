"""Verify macOS service installation in a temporary home with launchctl mocked."""
import contextlib
import importlib.util
import io
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "player_service", Path(__file__).resolve().parents[1] / "scripts/macos/service.py")
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)


class ServiceTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.home = Path(directory.name).resolve()
        self.binary = self.home / "Player & Music" / "player"
        self.binary.parent.mkdir()
        self.binary.touch()
        self.binary.chmod(0o755)
        self.contexts = contextlib.ExitStack()
        self.addCleanup(self.contexts.close)
        self.contexts.enter_context(mock.patch.object(service.Path, "home", return_value=self.home))
        self.contexts.enter_context(mock.patch.object(service.os, "getuid", return_value=501, create=True))
        self.contexts.enter_context(mock.patch.object(service.os, "access", return_value=True))
        self.run = self.contexts.enter_context(mock.patch.object(service.subprocess, "run"))
        self.contexts.enter_context(contextlib.redirect_stdout(io.StringIO()))
        self.contexts.enter_context(contextlib.redirect_stderr(io.StringIO()))

    def invoke(self, *args):
        service.main(["--executable", str(self.binary), *args])

    def plist(self, instance=None):
        suffix = f".{instance}" if instance else ""
        return self.home / "Library/LaunchAgents" / f"org.lyrion.player{suffix}.plist"

    def definition(self, instance=None):
        with self.plist(instance).open("rb") as source:
            return plistlib.load(source)

    def test_named_services_update_and_uninstall_independently(self):
        self.invoke("--instance", "server-a", "--server", "192.168.1.10",
                    "--name", 'Music & "Room A"', "--mac", "02:00:00:00:00:01")
        self.invoke("--instance", "server-b", "--server", "192.168.1.20",
                    "--mac", "02:00:00:00:00:02")
        original_b = self.plist("server-b").read_bytes()
        a = self.definition("server-a")
        b = self.definition("server-b")
        self.assertEqual(a["ProgramArguments"], [str(self.binary), "-n", 'Music & "Room A"',
                         "-d", "output=info", "-s", "192.168.1.10", "-m", "02:00:00:00:00:01"])
        self.assertEqual(a["Label"], "org.lyrion.player.server-a")
        self.assertNotEqual(a["StandardErrorPath"], b["StandardErrorPath"])
        self.assertEqual(a["StandardErrorPath"],
                         str(self.home / "Library/Logs/lyrion-player/player-server-a.log"))
        self.assertTrue(a["KeepAlive"])
        self.assertTrue(a["RunAtLoad"])
        self.assertEqual(self.run.call_args.args[0],
                         ["launchctl", "bootstrap", "gui/501", str(self.plist("server-b"))])

        self.run.reset_mock()
        self.invoke("--instance", "server-a", "--server", "192.168.1.30",
                    "--mac", "02:00:00:00:00:01")
        self.assertIn("192.168.1.30", self.definition("server-a")["ProgramArguments"])
        self.assertEqual(self.plist("server-b").read_bytes(), original_b)
        self.assertEqual(self.run.call_args_list[0].args[0],
                         ["launchctl", "bootout", "gui/501/org.lyrion.player.server-a"])

        # Removal must not depend on the executable still being installed.
        self.binary.unlink()
        service.main(["--instance", "server-a", "--uninstall"])
        self.assertFalse(self.plist("server-a").exists())
        self.assertEqual(self.plist("server-b").read_bytes(), original_b)
        self.assertEqual(self.run.call_args.args[0],
                         ["launchctl", "bootout", "gui/501/org.lyrion.player.server-a"])

    def test_original_service_keeps_label_log_and_default_identity(self):
        self.invoke()
        default = self.definition()
        self.assertEqual(default["Label"], "org.lyrion.player")
        self.assertEqual(default["StandardErrorPath"], str(self.home / "Library/Logs/lyrion-player/player.log"))
        self.assertNotIn("-m", default["ProgramArguments"])
        self.assertNotIn("-s", default["ProgramArguments"])
        self.invoke("--instance", "player", "--mac", "02:00:00:00:00:01")
        self.assertNotEqual(default["StandardErrorPath"], self.definition("player")["StandardErrorPath"])
        service.main(["--uninstall"])
        self.assertFalse(self.plist().exists())
        self.assertTrue(self.plist("player").exists())

    def test_invalid_arguments_do_not_touch_startup_entries(self):
        for args in (["--instance", "../escape"], ["--instance", "server-a"],
                     ["--mac", "not-a-mac"], ["--mac", "00:00:00:00:00:00"],
                     ["--mac", "01:00:00:00:00:01"]):
            with self.subTest(args=args), self.assertRaises(SystemExit) as result:
                self.invoke(*args)
            self.assertEqual(result.exception.code, 2)
        self.run.assert_not_called()
        self.assertFalse((self.home / "Library").exists())


if __name__ == "__main__":
    unittest.main()
