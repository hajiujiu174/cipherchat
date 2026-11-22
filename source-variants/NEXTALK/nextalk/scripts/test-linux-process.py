"""Verify the Linux server's TLS listener, restart, and graceful SIGTERM in isolated data."""
import argparse
import json
import signal
import socket
import ssl
import struct
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('server', type=Path)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
binary = args.server.resolve()
directory = args.directory.resolve()
directory.mkdir(parents=True, exist_ok=False)
listener = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
listener.bind(('::1', 0))
port = listener.getsockname()[1]
listener.close()


def read_exact(connection, size):
    result = b''
    while len(result) < size:
        block = connection.recv(size-len(result))
        if not block:
            raise RuntimeError('unexpected EOF')
        result += block
    return result


def hello(context):
    with socket.create_connection(('::1', port), timeout=2) as raw:
        with context.wrap_socket(raw, server_hostname='localhost') as tls:
            assert tls.version() == 'TLSv1.3'
            frame = json.dumps({'type': 'hello', 'version': 2}).encode()
            tls.sendall(struct.pack('!I', len(frame)) + frame)
            length = struct.unpack('!I', read_exact(tls, 4))[0]
            reply = json.loads(read_exact(tls, length))
            assert reply['type'] == 'hello_result' and reply['ok'] is True


for attempt in range(2):
    with (directory / f'server-{attempt}.log').open('wb') as log:
        server = subprocess.Popen([str(binary), str(port), 'server.db', 'cert.pem', 'key.pem', '::1'], cwd=directory, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            ready = False
            while time.monotonic() < deadline and server.poll() is None:
                try:
                    context = ssl.create_default_context(cafile=str(directory/'cert.pem'))
                    context.minimum_version = ssl.TLSVersion.TLSv1_3
                    hello(context)
                    ready = True
                    break
                except (OSError, ssl.SSLError):
                    time.sleep(0.05)
            assert ready, 'server did not complete verified TLS negotiation'
            # Disconnect during handshake; the listener must remain available.
            with socket.create_connection(('::1', port), timeout=2):
                pass
            hello(context)
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=10) == 0
        finally:
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=10)

evidence = {'ipv6_tls13': True, 'v2_hello': True, 'abrupt_peer_close_survived': True,
            'restart_with_existing_database_and_certificate': True, 'sigterm_exit_codes': [0, 0]}
(directory/'result.json').write_text(json.dumps(evidence, indent=2))
print(json.dumps(evidence))
