"""Exercise consent over TLS on an isolated Linux server/database; never touches live accounts."""
import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import time

def b64(value):
    return base64.b64encode(value).decode()

class Wire:
    def __init__(self, host, port, certificate, name):
        context = ssl.create_default_context(cafile=str(certificate))
        self.socket = context.wrap_socket(socket.create_connection((host, port), timeout=10), server_hostname=name)
        self.request("hello", "hello_result")

    def request(self, kind, expected, **fields):
        data = json.dumps(dict(type=kind, version=2, **fields)).encode()
        self.socket.sendall(struct.pack("!I", len(data)) + data)
        return self.receive(expected)

    def receive(self, expected):
        def read(size):
            result = b""
            while len(result) < size:
                part = self.socket.recv(size - len(result))
                if not part:
                    raise EOFError("server disconnected")
                result += part
            return result
        for _ in range(100):
            size, = struct.unpack("!I", read(4))
            assert 0 < size <= 2 * 1024 * 1024
            value = json.loads(read(size))
            if value["type"] == expected:
                return value
            assert value["type"] in ("presence", "contact_list"), value
        raise RuntimeError("expected response missing")

    def close(self):
        self.socket.close()

def probe(host, port, certificate, name):
    client = Wire(host, port, certificate, name)
    try:
        for kind, fields in [("list_contacts", {}), ("search_users", {"query": ""}), ("respond_friend", {"username": "nobody", "accept": True})]:
            response = client.request(kind, "error", **fields)
            assert response["code"] == "not_logged_in", response
        return {"contact_protocol": True, "unauthenticated_denied": True}
    finally:
        client.close()

def isolated(binary, work):
    work.mkdir(parents=True, exist_ok=False)
    users = {}
    for name in ("alice", "bob", "carol"):
        key = work / (name + ".pem")
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(key)], check=True, capture_output=True)
        pub = subprocess.check_output(["openssl", "pkey", "-in", str(key), "-pubout", "-outform", "DER"])[-32:]
        users[name] = (key, pub)
    def sign(name, data):
        path = work / (name + ".sign-input"); path.write_bytes(data)
        return subprocess.check_output(["openssl", "pkeyutl", "-sign", "-inkey", str(users[name][0]), "-rawin", "-in", str(path)])
    def login(name, port, register=False):
        wire = Wire("127.0.0.1", port, work / "cert.pem", "localhost")
        if register:
            prekey = os.urandom(32)
            assert wire.request("register", "register_result", username=name, password="isolated-test-password", sign_pub=b64(users[name][1]), dh_pub=b64(os.urandom(32)), signed_prekey=b64(prekey), spk_sig=b64(sign(name, prekey)))["ok"]
        challenge = wire.request("login_request", "login_challenge", username=name)
        nonce = base64.b64decode(challenge["nonce"])
        key = hashlib.pbkdf2_hmac("sha256", b"isolated-test-password", base64.b64decode(challenge["salt"]), challenge["iterations"], 32)
        prk = hmac.digest(b"CipherChat/Login/v1", key, "sha256")
        proof = hmac.digest(prk, nonce + b"\x01", "sha256")
        assert wire.request("login_response", "login_result", username=name, nonce=b64(nonce), proof=b64(proof), sig=b64(sign(name, nonce)))["ok"]
        return wire
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0)); port = reservation.getsockname()[1]
    # Restart the exact binary and database after an offline request and after acceptance.
    for stage in range(3):
        with (work / ("server-" + str(stage) + ".log")).open("wb") as log:
            child = subprocess.Popen([str(binary), str(port), "server.db", "cert.pem", "key.pem", "127.0.0.1"], cwd=work, stdout=log, stderr=log)
            clients = []
            try:
                for _ in range(100):
                    try:
                        probe("127.0.0.1", port, work / "cert.pem", "localhost"); break
                    except (ConnectionRefusedError, FileNotFoundError):
                        time.sleep(.1)
                a = login("alice", port, stage == 0); clients.append(a)
                b = login("bob", port, stage == 0); clients.append(b)
                if stage == 0:
                    c = login("carol", port, True); clients.append(c)
                    assert a.request("list_contacts", "contact_list")["users"] == []
                    assert b.request("list_contacts", "contact_list")["users"] == []
                    search = a.request("search_users", "search_result", query="bo", after="", msg_id="test-query")
                    assert search["msg_id"] == "test-query" and [u["username"] for u in search["users"]] == ["bob"]
                    b.close(); clients.remove(b)
                    assert a.request("add_friend", "friend_result", username="bob")["state"] == "pending"
                    assert not c.request("respond_friend", "friend_result", username="alice", accept=True)["ok"]
                    assert a.request("list_contacts", "contact_list")["users"] == []
                elif stage == 1:
                    assert b.request("list_contacts", "contact_list")["incoming"] == ["alice"]
                    assert b.request("respond_friend", "friend_result", username="alice", accept=False)["state"] == "declined"
                    assert a.request("add_friend", "friend_result", username="bob")["state"] == "pending"
                    assert b.request("respond_friend", "friend_result", username="alice", accept=True)["state"] == "accepted"
                else:
                    for wire, friend in [(a, "bob"), (b, "alice")]:
                        directory = wire.request("list_contacts", "contact_list")
                        assert [u["username"] for u in directory["users"]] == [friend]
                        assert directory["incoming"] == [] and directory["outgoing"] == []
            finally:
                for client in clients:
                    client.close()
                child.terminate(); child.wait(timeout=10)
    return {"isolated_tls": True, "request_reject_accept": True, "recipient_authorization": True, "offline_request_restart": True, "friendship_restart": True}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path); parser.add_argument("--work", type=Path)
    parser.add_argument("--host", default="2603:1040:f02:d::21d"); parser.add_argument("--port", type=int, default=55220)
    parser.add_argument("--certificate", type=Path); parser.add_argument("--server-name", default="2603:1040:f02:d::21d")
    args = parser.parse_args()
    result = isolated(args.binary.resolve(), args.work.resolve()) if args.binary else probe(args.host, args.port, args.certificate, args.server_name)
    print(json.dumps(result))
