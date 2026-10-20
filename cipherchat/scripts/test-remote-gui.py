"""Exercise two real Windows GUI clients against the deployed IPv6 server.

Uses test-owned accounts and data; does not install software or change the host.
Only public screenshots and a result summary are copied into the evidence folder.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import struct
import subprocess
import time


def read_exact(stream, count):
    result = b""
    while len(result) < count:
        chunk = stream.recv(count - len(result))
        if not chunk:
            raise RuntimeError("TLS peer closed before a complete frame")
        result += chunk
    return result


def verify_tls(host, port, cert, server_name, connection_path):
    context = ssl.create_default_context(cafile=str(cert))
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
    with socket.create_connection((host, port), timeout=10) as raw:
        if raw.family != socket.AF_INET6:
            raise RuntimeError("Expected an IPv6 connection")
        with context.wrap_socket(raw, server_hostname=server_name) as channel:
            fingerprint = hashlib.sha256(channel.getpeercert(binary_form=True)).hexdigest()
            hello = json.dumps({"type": "hello", "version": 2}).encode()
            channel.sendall(struct.pack("!I", len(hello)) + hello)
            length, = struct.unpack("!I", read_exact(channel, 4))
            if length > 8192:
                raise RuntimeError("Unexpected hello frame length")
            reply = json.loads(read_exact(channel, length))
            if reply.get("type") != "hello_result" or reply.get("version") != 2 or reply.get("ok") is not True:
                raise RuntimeError(f"v2 negotiation rejected: {reply}")
            result = {"ipv6": True, "connection_path": connection_path,
                      "public_endpoint": connection_path == "public", "tls": channel.version(),
                      "v2_hello": True, "certificate_sha256": fingerprint}
    try:
        with socket.create_connection((host, port), timeout=10) as raw:
            with context.wrap_socket(raw, server_hostname="wrong-name.invalid"):
                raise RuntimeError("An incorrect certificate name was accepted")
    except ssl.SSLCertVerificationError as error:
        if error.verify_code != 62:
            raise
        result["wrong_certificate_name_rejected"] = True
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="2603:1040:f02:d::21d")
    parser.add_argument("--port", type=int, default=55220)
    parser.add_argument("--server-name", help="TLS certificate name; defaults to host")
    parser.add_argument("--connection-path", choices=("public", "ssh-tunnel"), default="public",
                        help="Label the actual path honestly; a tunnel is not public-port verification")
    parser.add_argument("--cert", required=True, type=Path)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--resume-from", type=Path, help="Reuse existing accounts and profiles to check chat after a server restart")
    parser.add_argument("--source-file", type=Path, help="Use the same input file for before/after throughput comparisons")
    parser.add_argument("--relay-only", action="store_true", help="Disable peer file listeners for this run")
    parser.add_argument("--account-scope", default="", help="Explicit account scope retained across an authorized server migration")
    parser.add_argument("--qt-bin", default=r"C:\Qt\6.11.2\mingw_64\bin")
    parser.add_argument("--mingw-bin", default=r"D:\msys64\mingw64\bin")
    args = parser.parse_args()
    args.server_name = args.server_name or args.host
    args.cert = args.cert.resolve(strict=True)
    args.driver = args.driver.resolve(strict=True)
    work = args.work_dir.resolve()
    evidence = args.evidence_dir.resolve()
    if work.exists() or evidence.exists():
        raise RuntimeError("Use new work and evidence directories; existing runs are preserved")
    work.mkdir(parents=True)
    evidence.mkdir(parents=True)
    report = {"host": args.host, "port": args.port, "physical_client_machines": 1, "client_processes": 2}
    processes, logs, configs = [], [], []
    started = time.monotonic()
    try:
        report["connection"] = verify_tls(args.host, args.port, args.cert, args.server_name, args.connection_path)
        source = work / "remote-source.bin"
        if not args.resume_from:
            if args.source_file:
                shutil.copyfile(args.source_file.resolve(strict=True), source)
            else:
                source.write_bytes(secrets.token_bytes(1024 * 1024 + 15))
        account_id = secrets.token_hex(5)
        usernames = [f"verify_{account_id}_{role}" for role in ("a", "b")]
        messages = ["跨公网 IPv6：你好，这是加密私聊测试。", "已收到，远程服务器的消息签收正常。"]
        if args.resume_from:
            messages = [f"服务重启后使用原账号和会话发送新消息 {account_id}", f"重启恢复签收正常 {account_id}"]
        environment = dict(os.environ)
        environment["PATH"] = args.mingw_bin + os.pathsep + args.qt_bin + os.pathsep + environment.get("PATH", "")
        for index in range(2):
            role = ("alice", "bob")[index]
            config = {"sending": index == 0, "username": usernames[index], "peer": usernames[1-index],
                      "password": secrets.token_urlsafe(32), "host": args.host, "port": args.port,
                      "server_name": args.server_name, "connect_timeout_ms": 10000, "workflow_timeout_ms": 120000,
                      "ca": str(args.cert), "data": str(work / f"{role}-data"), "artifacts": str(evidence / role),
                      "source": str(source), "own_identity": str(work / f"{index}.identity"),
                      "peer_identity": str(work / f"{1-index}.identity"), "own_message": messages[index],
                      "peer_message": messages[1-index]}
            if args.relay_only:
                config["direct_enabled"] = False
            if args.account_scope:
                config["account_scope"] = args.account_scope
            if args.resume_from:
                previous = json.loads((args.resume_from / f"{role}.json").read_text(encoding="utf-8"))
                previous_scope = previous.get("account_scope") or f'{previous["host"]}:{previous["port"]}/{previous.get("server_name", previous["host"])}'
                if previous["host"] != args.host or previous["port"] != args.port:
                    if not args.account_scope or args.account_scope != previous_scope:
                        raise RuntimeError("Endpoint migration must explicitly retain the original account scope")
                for key in ("username", "peer", "password", "data"):
                    config[key] = previous[key]
                config.update({"chat_only": True, "register_new": False, "expected_own_identity": previous["own_identity"]})
                usernames[index] = previous["username"]
            path = work / f"{role}.json"
            path.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
            configs.append(path)
            log = (work / f"{role}.log").open("wb")
            logs.append(log)
            processes.append(subprocess.Popen([str(args.driver), str(path)], cwd=work, env=environment,
                                               stdout=log, stderr=subprocess.STDOUT,
                                               creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic() + 130
        while time.monotonic() < deadline:
            for path, process in zip(configs, processes):
                error = Path(str(path) + ".error")
                if error.exists():
                    raise RuntimeError(error.read_text(encoding="utf-8"))
                if process.poll() is not None:
                    raise RuntimeError(f"GUI exited prematurely: {path.name}, code={process.returncode}")
            if all(Path(str(path) + ".done").exists() for path in configs):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("Remote GUI workflow timed out")
        report["gui"] = []
        for path in configs:
            result = json.loads(Path(str(path) + ".done").read_text(encoding="utf-8"))
            identity_and_transfer = (result["existing_identity_matched"] if args.resume_from
                                     else result["identity_confirmed"] and result["progress_seen"])
            if not (identity_and_transfer and result["stage"] == 6 and result["gui_heartbeats"] > 20
                    and result["max_gui_gap_ms"] < 1500):
                raise RuntimeError(f"GUI acceptance failed: {result}")
            report["gui"].append(result)
            Path(str(path) + ".stop").write_text("verified", encoding="ascii")
        for process in processes:
            if process.wait(timeout=10) != 0:
                raise RuntimeError("GUI did not close successfully")
        report.update({"passed": True, "accounts": usernames, "bidirectional_text_receipts": True,
                       "client_exit_codes": [p.returncode for p in processes]})
        if args.resume_from:
            report.update({"existing_accounts_logged_in": True, "original_identities_preserved": True,
                           "existing_sessions_reused": all(not r["identity_confirmed"] for r in report["gui"])})
        else:
            downloads = [path for path in (work / "bob-data").glob("*/downloads/*")
                         if path.is_file() and path.name.endswith("_received_" + source.name)]
            if len(downloads) != 1 or downloads[0].read_bytes() != source.read_bytes():
                raise RuntimeError("Received file does not exactly match the sent file")
            report.update({"file_bytes": source.stat().st_size, "file_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                           "received_file_matches": True, "group_message": True})
            milliseconds = max(result.get("file_transfer_ms", 0) for result in report["gui"])
            if milliseconds:
                report.update({"file_transfer_ms": milliseconds, "file_kib_per_second": round(source.stat().st_size * 1000 / milliseconds / 1024, 2)})
    except Exception as error:
        report.update({"passed": False, "error": str(error)})
        raise
    finally:
        for path in configs:
            Path(str(path) + ".stop").touch()
        for process in processes:
            if process.poll() is None:
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=10)
        for log in logs:
            log.close()
        report["elapsed_seconds"] = round(time.monotonic() - started, 2)
        (evidence / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
