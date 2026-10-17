"""Collect multiple metadata replies from an isolated mock discovery endpoint."""
import socket
import subprocess
import sys
from pathlib import Path

helper = str(Path(sys.argv[1]).resolve())
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
    server.bind(('127.0.0.1', 0))
    server.settimeout(5)
    process = subprocess.Popen([helper, '--scan', str(server.getsockname()[1])],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        query, peer = server.recvfrom(2048)
        assert query == b'eNAME\0UUID\0JSON\0', query
        server.sendto(b'ENAME\4HomeUUID\3abcJSON\49001', peer)
        server.sendto(b'ENAME\6OfficeUUID\3defJSON\49002', peer)
        server.sendto(b'ENAME\7broken', peer)  # Truncated metadata must be ignored.
        output, error = process.communicate(timeout=5)
        assert process.returncode == 0, error
        assert b'server=Home uuid=abc web=9001' in output, output
        assert b'server=Office uuid=def web=9002' in output, output
        assert b'server=broken' not in output, output
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
