"""Managed IPC lifecycle checks against the silent, real protocol backend."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import unittest

PLAYER = str(Path(sys.argv.pop(1)).resolve())


class ManagedTests(unittest.TestCase):
    def launch(self, *args):
        isolated = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        isolated.bind(('127.0.0.1', 0))
        self.addCleanup(isolated.close)
        process = subprocess.Popen([PLAYER, '--managed', '--name', 'Managed Test',
            '--mac', '02:00:00:00:fe:01', *args], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env={**os.environ, 'PLAYER_TEST_SLIMPROTO_PORT': str(isolated.getsockname()[1])})
        self.addCleanup(lambda: process.poll() is None and process.kill())
        return process

    def check_exit(self, process, command):
        output, errors = process.communicate(command, timeout=5)
        self.assertEqual(process.returncode, 0, errors.decode(errors='replace'))
        events = [json.loads(line) for line in output.splitlines()]
        self.assertTrue(events, errors.decode(errors='replace'))
        self.assertTrue(all(e['version'] == 1 for e in events))
        self.assertEqual(events[-1]['state'], 'stopped')
        return events

    def test_stop_during_discovery(self):
        # Isolated UDP port ensures discovery cannot contact a real music server.
        self.check_exit(self.launch(), b'stop\n')

    def test_owner_eof_during_discovery(self):
        self.check_exit(self.launch(), b'')

    def test_stop_during_retry(self):
        with socket.socket() as unused:
            unused.bind(('127.0.0.1', 0))
            process = self.launch('--server', f'127.0.0.1:{unused.getsockname()[1]}')
            self.check_exit(process, b'stop\n')

    def test_connected_status_and_stop(self):
        with socket.socket() as server:
            server.bind(('127.0.0.1', 0))
            server.listen()
            server.settimeout(5)
            process = self.launch('--server', f'127.0.0.1:{server.getsockname()[1]}')
            with server.accept()[0] as connection:
                connection.settimeout(5)
                self.assertEqual(connection.recv(4), b'HELO')
                events = self.check_exit(process, b'stop\n')
                connected = [e for e in events if e['type'] == 'connection' and e['state'] == 'connected']
                self.assertEqual(connected[0]['host'], '127.0.0.1')
                self.assertEqual(connected[0]['port'], server.getsockname()[1])

    def test_stdout_audio_rejected(self):
        process = self.launch('--output', '-')
        output, error = process.communicate(b'', timeout=5)
        self.assertNotEqual(process.returncode, 0)
        self.assertIn(b'--managed cannot use', error)


if __name__ == '__main__':
    unittest.main()
