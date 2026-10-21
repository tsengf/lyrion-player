"""Independent backend interoperability and hostile-input checks; no real server/audio."""
import contextlib
import http.server
import io
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import wave

PLAYER = str(Path(sys.argv.pop(1)).resolve())
FIXTURES = Path(__file__).parent / 'fixtures'


def exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError('Player disconnected')
        data.extend(chunk)
    return bytes(data)


def packet(sock):
    opcode, size = struct.unpack('!4sI', exact(sock, 8))
    return opcode, exact(sock, size)


def send(sock, data):
    sock.sendall(struct.pack('!H', len(data)) + data)


def command(code, value=0):
    return b'strm' + code + bytes(13) + struct.pack('!I', value) + bytes(6)


def start(port, codec=b'p', mode=b'1', rate=b'3', size=b'1', channels=b'2', endian=b'1'):
    return (b'strms' + mode + codec + size + rate + channels + endian +
            bytes([1, 0, 0, ord('0'), 0, 1, 0]) + struct.pack('!IH4s', 0, port, bytes(4)) +
            b'GET /audio HTTP/1.0\r\n\r\n')


def until(sock, opcode, event=None, timeout=6):
    deadline = time.monotonic() + timeout
    while True:
        sock.settimeout(max(.01, deadline - time.monotonic()))
        op, data = packet(sock)
        if op == opcode and (event is None or data[:4] == event):
            return data


class ProtocolTests(unittest.TestCase):
    @contextlib.contextmanager
    def player(self):
        with socket.socket() as listener, tempfile.TemporaryFile() as logs:
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            listener.settimeout(6)
            process = subprocess.Popen([PLAYER, '--managed', '--server',
                f'127.0.0.1:{listener.getsockname()[1]}', '--name', 'Independent Test',
                '--mac', '02:00:00:00:01:04'], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=logs)
            try:
                with listener.accept()[0] as connection:
                    connection.settimeout(6)
                    hello = until(connection, b'HELO')
                    self.assertEqual(hello[:8], bytes([12, 0, 2, 0, 0, 0, 1, 4]))
                    self.assertIn(b'Model=lyrion-player', hello[36:])
                    self.assertNotIn(b'squeezelite', hello)
                    yield connection, process, listener, logs
            except Exception:
                logs.seek(0)
                print(logs.read().decode(errors='replace'), file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    output, _ = process.communicate(b'stop\n', timeout=6)
                    self.assertEqual(process.returncode, 0)
                    self.assertEqual(json.loads(output.splitlines()[-1])['state'], 'stopped')

    @contextlib.contextmanager
    def http(self, body, chunked=False, stall=False):
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200)
                if chunked:
                    self.send_header('Transfer-Encoding', 'chunked')
                elif not stall:
                    self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                try:
                    if stall:
                        stop.wait(4)
                    elif chunked:
                        for offset in range(0, len(body), 137):
                            part = body[offset:offset+137]
                            self.wfile.write(f'{len(part):x};test=1\r\n'.encode() + part + b'\r\n')
                        self.wfile.write(b'0\r\nX-Test: trailer\r\n\r\n')
                    else:
                        self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def log_message(self, *args):
                pass

        stop = threading.Event()
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server.server_port
        finally:
            stop.set()
            server.shutdown()
            server.server_close()
            thread.join()

    def test_fragmented_and_coalesced_commands(self):
        with self.player() as (connection, *_):
            data = b'setd\x00'
            wire = struct.pack('!H', len(data)) + data
            for byte in wire:
                connection.sendall(bytes([byte]))
            self.assertEqual(until(connection, b'SETD'), b'\0Independent Test\0')
            renamed = b'setd\0New Name\0'
            connection.sendall(struct.pack('!H', len(renamed)) + renamed + b'\0\5setd\0')
            self.assertEqual(until(connection, b'SETD'), b'\0New Name\0')
            self.assertEqual(until(connection, b'SETD'), b'\0New Name\0')
            send(connection, command(b't', 0xfedcba98))
            body = until(connection, b'STAT', b'STMt')
            self.assertEqual(body[47:51], struct.pack('!I', 0xfedcba98))
            self.assertEqual(len(body), 53)

    def test_unknown_and_maximum_frames(self):
        with self.player() as (connection, *_):
            send(connection, b'zzzz' + bytes(65531))
            send(connection, b'setd\0')
            self.assertEqual(until(connection, b'SETD'), b'\0Independent Test\0')

    def test_bad_frames_disconnect_and_reconnect(self):
        for payload in (b'\0\0', b'\0\3bad', b'\0\4audg',
                        struct.pack('!H', 7) + b'setd\0xx'):
            with self.subTest(payload=payload), self.player() as (connection, _, listener, __):
                connection.sendall(payload)
                while connection.recv(1024):
                    pass
                with listener.accept()[0] as replacement:
                    replacement.settimeout(5)
                    hello = until(replacement, b'HELO')
                    self.assertEqual(hello[24] & 0x40, 0x40)
                    send(replacement, b'setd\0')
                    self.assertEqual(until(replacement, b'SETD'), b'\0Independent Test\0')

    def test_codec_playback(self):
        for codec, filename in ((b'f', 'tone.flac'), (b'm', 'tone.mp3'), (b'o', 'tone.ogg')):
            with self.subTest(codec=codec), self.http((FIXTURES / filename).read_bytes(), chunked=True) as port:
                with self.player() as (connection, _, __, logs):
                    send(connection, start(port, codec))
                    seen = set()
                    while b'STMu' not in seen:
                        opcode, data = packet(connection)
                        if opcode == b'STAT':
                            seen.add(data[:4])
                    self.assertTrue({b'STMs', b'STMd', b'STMu'} <= seen, seen)
                    self.assertNotIn(b'STMn', seen)
                    logs.seek(0)
                    text = logs.read().decode()
                    self.assertIn('Test backend received nonzero PCM samples', text)
                    self.assertIn('resampling from 44100 -> 48000', text)

    def test_wave_aiff_and_high_rate_pcm(self):
        samples = struct.pack('<hh', 5000, -3000) * 24000
        wav = io.BytesIO()
        with wave.open(wav, 'wb') as f:
            f.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            f.writeframes(samples)
        comm = struct.pack('!hIh', 2, 24000, 16) + bytes.fromhex('400ebb80000000000000')
        aiff_body = b'COMM' + struct.pack('!I', len(comm)) + comm
        aiff_body += b'SSND' + struct.pack('!I', len(samples)+8) + bytes(8) + struct.pack('!hh', 5000, -3000)*24000
        aiff = b'FORM' + struct.pack('!I', len(aiff_body)+4) + b'AIFF' + aiff_body
        unknown_length = bytearray(wav.getvalue())
        unknown_length[40:44] = bytes([255])*4
        for body, rate in ((wav.getvalue(), b'?'), (bytes(unknown_length), b'?'),
                           (aiff, b'?'), (samples*4, b'<'), (samples*8, b'>')):
            with self.subTest(rate=rate, header=body[:4]), self.http(body) as port:
                with self.player() as (connection, *_):
                    send(connection, start(port, rate=rate))
                    seen = set()
                    while b'STMu' not in seen:
                        opcode, data = packet(connection)
                        if opcode == b'STAT':
                            seen.add(data[:4])
                    self.assertIn(b'STMs', seen)
                    self.assertIn(b'STMd', seen)
                    self.assertNotIn(b'STMn', seen)

    def test_continuation_codec_and_metadata(self):
        pcm = struct.pack('<hh', 5000, -3000)*22050
        title = b"StreamTitle='Test';".ljust(32, b'\0')
        wire = bytearray()
        for offset in range(0, len(pcm), 4096):
            part = pcm[offset:offset+4096]
            wire.extend(part)
            if len(part) == 4096:
                wire.extend(bytes([2]) + title)
        with self.http(bytes(wire), chunked=True) as port, self.player() as (connection, *_):
            send(connection, start(port, codec=b'?', mode=b'3'))
            self.assertIn(b'200', until(connection, b'RESP'))
            send(connection, b'codcp1321')
            send(connection, b'cont' + struct.pack('!I', 4096) + b'\0')
            metadata = until(connection, b'META')
            self.assertEqual(metadata, title)
            until(connection, b'STAT', b'STMu')

    def test_buffered_start_pause_resume_and_flush(self):
        body = struct.pack('<hh', 5000, -3000)*44100*3
        with self.http(body) as port, self.player() as (connection, *_):
            send(connection, start(port, mode=b'0'))
            until(connection, b'STAT', b'STMl')
            send(connection, command(b't', 1))
            body = until(connection, b'STAT', b'STMt')
            self.assertEqual(struct.unpack_from('!I', body, 43)[0], 0)
            send(connection, command(b'u'))
            until(connection, b'STAT', b'STMs')
            time.sleep(.25)
            send(connection, command(b'p'))
            until(connection, b'STAT', b'STMp')
            send(connection, command(b't', 2))
            first = until(connection, b'STAT', b'STMt')[43:47]
            time.sleep(.15)
            send(connection, command(b't', 3))
            second = until(connection, b'STAT', b'STMt')[43:47]
            self.assertEqual(first, second)
            send(connection, command(b'u'))
            until(connection, b'STAT', b'STMr')
            send(connection, command(b'q'))
            until(connection, b'STAT', b'STMf')
            send(connection, command(b't', 4))
            self.assertEqual(until(connection, b'STAT', b'STMt')[43:47], bytes(4))

    def test_track_queue_and_end_of_stream(self):
        body = struct.pack('<hh', 5000, -3000)*44100
        with self.http(body) as port, self.player() as (connection, *_):
            send(connection, start(port))
            seen_start = False
            while True:
                op, body = packet(connection)
                if op == b'STAT' and body[:4] == b'STMs':
                    seen_start = True
                if op == b'STAT' and body[:4] == b'STMd':
                    break
            send(connection, start(port))
            starts = int(seen_start)
            while starts < 2:
                until(connection, b'STAT', b'STMs')
                starts += 1
            until(connection, b'STAT', b'STMu')

    def test_cancel_blocked_body_and_continuation(self):
        for mode in (b'1', b'3'):
            with self.subTest(mode=mode), self.http(b'', stall=True) as port:
                with self.player() as (connection, process, *_):
                    send(connection, start(port, mode=mode))
                    until(connection, b'RESP')
                    started = time.monotonic()
                    output, _ = process.communicate(b'stop\n', timeout=3)
                    self.assertEqual(process.returncode, 0)
                    self.assertLess(time.monotonic()-started, 3)
                    self.assertEqual(json.loads(output.splitlines()[-1])['state'], 'stopped')

    def test_cancel_full_output_buffer(self):
        body = struct.pack('<hh', 5000, -3000)*44100*20
        with self.http(body) as port, self.player() as (connection, process, *_):
            send(connection, start(port, mode=b'0'))
            until(connection, b'STAT', b'STMl')
            time.sleep(.15)
            output, _ = process.communicate(b'stop\n', timeout=3)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(json.loads(output.splitlines()[-1])['state'], 'stopped')

    def test_server_redirect(self):
        with self.player() as (connection, _, listener, __):
            send(connection, b'serv' + socket.inet_aton('127.0.0.1') + b'1234567890')
            with listener.accept()[0] as redirected:
                redirected.settimeout(5)
                hello = until(redirected, b'HELO')
                self.assertEqual(hello[24] & 0x40, 0)
                self.assertIn(b'SyncgroupID=1234567890', hello[36:])

    def test_unsupported_codec_and_truncated_pcm(self):
        for body, codec in ((bytes(1), b'x'), (bytes(13), b'p')):
            with self.subTest(codec=codec), self.http(body) as port, self.player() as (connection, *_):
                send(connection, start(port, codec=codec))
                until(connection, b'STAT', b'STMn')
                send(connection, command(b't', 42))
                self.assertEqual(until(connection, b'STAT', b'STMt')[47:51], struct.pack('!I', 42))


if __name__ == '__main__':
    unittest.main()
