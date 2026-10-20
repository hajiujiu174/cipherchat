"""Exercise the real update wire protocol without registering or sending chat messages."""
import argparse
import base64
import hashlib
import json
import socket
import ssl
import struct
import subprocess
import time
import os
from pathlib import Path

def probe(host, port, certificate, server_name):
    context = ssl.create_default_context(cafile=str(certificate))
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    with socket.create_connection((host, port), timeout=15) as tcp, context.wrap_socket(tcp, server_hostname=server_name) as tls:
        def request(message):
            data = json.dumps(message).encode()
            tls.sendall(struct.pack("!I", len(data)) + data)
            def read(count):
                value = b""
                while len(value) < count:
                    part = tls.recv(count - len(value))
                    if not part:
                        raise EOFError("Update connection closed")
                    value += part
                return value
            size, = struct.unpack("!I", read(4))
            if size > 2 * 1048576:
                raise ValueError("Oversize response")
            return json.loads(read(size))
        latest_response = request(dict(type="update_get", msg_id="probe", path="latest.json"))
        assert latest_response["type"] == "update_result" and latest_response["ok"]
        manifest = json.loads(base64.b64decode(latest_response["data"], validate=True))
        block = next(b for f in manifest["files"] for b in f["blocks"])
        response = request(dict(type="update_get", msg_id="probe", path="blobs/" + block["sha256"]))
        data = base64.b64decode(response["data"], validate=True)
        assert len(data) == block["size"] and hashlib.sha256(data).hexdigest() == block["sha256"]
        denied = request(dict(type="update_get", msg_id="probe", path="../server.db"))
        assert denied["ok"] is False
        hello = request(dict(type="hello", version=2))
        assert hello["type"] == "hello_result" and hello["ok"]
        return dict(version=manifest["version"], files=len(manifest["files"]), tls=tls.version(), block_sha256=True, path_rejected=True, chat_hello=True)

def isolated(binary, feed, work):
    work.mkdir(parents=True, exist_ok=False)
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
    env = dict(os.environ, CIPHERCHAT_UPDATE_ROOT=str(feed))
    with (work / "server.log").open("wb") as log:
        child = subprocess.Popen([str(binary), str(port), "server.db", "cert.pem", "key.pem", "127.0.0.1"], cwd=work, env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError("Staging server exited")
                try:
                    return probe("127.0.0.1", port, work / "cert.pem", "localhost")
                except (ConnectionRefusedError, FileNotFoundError):
                    time.sleep(.1)
            raise TimeoutError("Staging server unavailable")
        finally:
            child.terminate(); child.wait(timeout=15)

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", default="2603:1040:f02:d::21d")
    p.add_argument("--port", type=int, default=55220)
    p.add_argument("--certificate", type=Path)
    p.add_argument("--server-name", default="2603:1040:f02:d::21d")
    p.add_argument("--isolated-binary", type=Path)
    p.add_argument("--feed", type=Path)
    p.add_argument("--work", type=Path)
    a = p.parse_args()
    result = isolated(a.isolated_binary, a.feed, a.work) if a.isolated_binary else probe(a.host, a.port, a.certificate, a.server_name)
    print(json.dumps(result))
