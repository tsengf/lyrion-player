"""Exercise the real SlimProto/PCM/resampling/output path with silent audio."""
import http.server
import math
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

PLAYER = str(Path(sys.argv.pop(1)).resolve())


def exact(sock, size):
    result = b""
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise EOFError("Player disconnected")
        result += chunk
    return result


def packet(sock):
    opcode, length = struct.unpack("!4sI", exact(sock, 8))
    return opcode, exact(sock, length)


def send(sock, payload):
    sock.sendall(struct.pack("!H", len(payload)) + payload)


class PlayerTests(unittest.TestCase):
    def test_loopback_discovery(self):
        # Binding only loopback means a LAN broadcast cannot satisfy this test.
        with socket.socket() as server, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery, \
                tempfile.TemporaryFile(mode="w+b") as logs:
            server.bind(("127.0.0.1", 0))
            server.listen()
            server.settimeout(8)
            port = server.getsockname()[1]
            discovery.bind(("127.0.0.1", port))
            discovery.settimeout(8)
            process = subprocess.Popen([PLAYER, "--name", "Discovery Test",
                "--mac", "02:00:00:00:00:03", "--debug", "slimproto=debug"],
                env={**os.environ, "PLAYER_TEST_SLIMPROTO_PORT": str(port)},
                stdout=logs, stderr=logs)
            try:
                request, address = discovery.recvfrom(256)
                self.assertEqual(request, b"e")
                discovery.sendto(b"invalid", address)
                server.settimeout(0.3)
                with self.assertRaises(socket.timeout):
                    connection, _ = server.accept()
                    connection.close()
                server.settimeout(8)
                discovery.sendto(b"E", address)
                with server.accept()[0] as connection:
                    connection.settimeout(5)
                    opcode, hello = packet(connection)
                    self.assertEqual(opcode, b"HELO")
                    self.assertIn(b"ModelName=Player", hello)
            except Exception:
                logs.seek(0)
                print(logs.read().decode(errors="replace"), file=sys.stderr)
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=7)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    def test_help_and_default_output(self):
        for flag in ("-?", "--help"):
            result = subprocess.run([PLAYER, flag], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0)
            self.assertIn(b"Player 0.1.0", result.stdout)
            self.assertIn(b"--server", result.stdout)
        for flag in ("-l", "--list-devices"):
            result = subprocess.run([PLAYER, flag], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0)
            self.assertIn(b"automatically follows changes", result.stdout)

    def test_reject_fixed_device(self):
        for flag, value in (("-o", "1"), ("--output", "1"),
                            ("--audio-params", "20"), ("--rates", "48000")):
            result = subprocess.run([PLAYER, flag, value], capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"requires default output", result.stderr)

    def test_invalid_long_options(self):
        for args in (["--unknown"], ["--server"], ["--name"], ["--debug"], ["--mac"]):
            result = subprocess.run([PLAYER, *args], capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"Option error", result.stderr)

    def test_long_logfile_option(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "player log.txt"
            result = subprocess.run([PLAYER, "--logfile", str(log), "--output", "1"],
                                    capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("requires default output", log.read_text())

    def test_pcm_playback_and_pause(self):
        # Nonzero stereo PCM at 44.1 kHz exercises conversion to our 48 kHz clock.
        pcm = b"".join(struct.pack("<hh", value, value) for value in
            (int(3000 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(44100 * 6)))

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200)
                self.send_header("Content-Length", str(len(pcm)))
                self.end_headers()
                self.wfile.write(pcm)

            def log_message(self, *args):
                pass

        http_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=http_server.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(http_server.server_close)
        self.addCleanup(http_server.shutdown)
        with socket.socket() as server, tempfile.TemporaryFile(mode="w+b") as logs:
            server.bind(("127.0.0.1", 0))
            server.listen()
            server.settimeout(10)
            process = subprocess.Popen([PLAYER, "--server", f"127.0.0.1:{server.getsockname()[1]}",
                "--mac", "02:00:00:00:00:01", "--name", "Player Test", "--debug", "all=info"],
                stdout=logs, stderr=logs)
            try:
                with server.accept()[0] as connection:
                    connection.settimeout(10)
                    opcode, hello = packet(connection)
                    self.assertEqual(opcode, b"HELO")
                    self.assertIn(b"ModelName=Player", hello)
                    send(connection, b"audg" + struct.pack("!IIBBII", 0, 0, 1, 0, 65536, 65536))
                    # 16-bit, 44.1 kHz, stereo, little endian; start immediately.
                    strm = b"strms1p1321" + bytes([1, 0, 0, ord('0'), 0, 1, 0])
                    strm += struct.pack("!IH4s", 0, http_server.server_port, socket.inet_aton("127.0.0.1"))
                    send(connection, strm + b"GET /audio HTTP/1.0\r\n\r\n")
                    events = set()
                    deadline = time.monotonic() + 10
                    while b"STMs" not in events and time.monotonic() < deadline:
                        opcode, body = packet(connection)
                        if opcode == b"STAT":
                            events.add(body[:4])
                    self.assertIn(b"STMs", events, "Output never started")
                    # A status query after playback must show an advancing audio clock.
                    time.sleep(0.5)
                    timestamp = struct.pack("!I", 0x12345678)
                    send(connection, b"strmt" + bytes(13) + timestamp + bytes(6))
                    deadline = time.monotonic() + 10
                    while True:
                        connection.settimeout(max(0.01, deadline - time.monotonic()))
                        opcode, body = packet(connection)
                        if opcode == b"STAT" and body[:4] == b"STMt" and body[47:51] == timestamp:
                            self.assertGreater(struct.unpack_from("!I", body, 43)[0], 0)
                            break
                    send(connection, b"strmp" + bytes(23))
                    deadline = time.monotonic() + 10
                    while True:
                        connection.settimeout(max(0.01, deadline - time.monotonic()))
                        opcode, body = packet(connection)
                        if opcode == b"STAT" and body[:4] == b"STMp":
                            break
            except Exception:
                logs.seek(0)
                print(logs.read().decode(errors="replace"), file=sys.stderr)
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            logs.seek(0)
            text = logs.read().decode(errors="replace")
            self.assertIn("resampling from 44100 -> 48000", text)
            self.assertIn("Test backend received nonzero PCM samples", text)


if __name__ == "__main__":
    unittest.main()
