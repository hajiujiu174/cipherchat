"""Verify the remote release manifest/ZIP and launch its actual Windows GUI."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import ssl
import subprocess
import time
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--client-binary", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--launch-count", type=int, default=3)
    parser.add_argument("--ui", choices=["glm", "codex", "rhine"], help="Exercise a specific packaged frontend")
    parser.add_argument("--expected-cert-sha256", default="623e21ecee300533999a305965f137d5ed07555d1155e420bfff5d9e98ab7765",
                        help="Trusted certificate fingerprint obtained separately from the package")
    args = parser.parse_args()
    package = args.package.resolve(strict=True)
    work = args.work_dir.resolve()
    evidence = args.evidence.resolve()
    if work.exists() or evidence.exists():
        raise RuntimeError("Choose new work/evidence paths; existing results are preserved")
    work.mkdir(parents=True)
    evidence.parent.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8-sig"))
    assert manifest["file_transport"] == "window-cbor-v1" and manifest["legacy_file_transfer"] is False
    entries = manifest["files"]
    assert len({entry["path"] for entry in entries}) == len(entries)
    for entry in entries:
        path = (package / entry["path"]).resolve(strict=True)
        assert path.is_relative_to(package)
        data = path.read_bytes()
        assert len(data) == entry["bytes"] and hashlib.sha256(data).hexdigest() == entry["sha256"]
        assert path.suffix.lower() not in {".db", ".key", ".sqlite", ".sqlite3"}
        assert path.suffix.lower() != ".pem" or path.name == "server-cert.pem"
    actual = {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()}
    assert actual == {entry["path"] for entry in entries} | {"manifest.json"}
    client = package / "cipherchat.exe"
    assert client.read_bytes() == args.client_binary.resolve(strict=True).read_bytes()
    cert_der = ssl.PEM_cert_to_DER_cert((package / "server-cert.pem").read_text())
    fingerprint = hashlib.sha256(cert_der).hexdigest()
    assert fingerprint == args.expected_cert_sha256.lower().replace(":", "")
    launcher = (package / "start-remote.cmd").read_text(encoding="utf-8-sig")
    if (package / "release.json").exists():
        assert 'cipherchat-launcher.exe' in launcher
        config = json.loads((package / 'update-config.json').read_text(encoding='utf-8-sig'))
        assert config == dict(host=manifest['server'], port=manifest['port'], server_name=manifest['server'])
    else:
        assert f'--host {manifest["server"]} ' in launcher
        assert f'--server-name {manifest["server"]} ' in launcher
        assert f'--port {manifest["port"]} ' in launcher
        assert '--ca "%~dp0server-cert.pem"' in launcher
    archive = Path(str(package) + ".zip")
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        files = [i for i in z.infolist() if not i.is_dir()]
        assert len(files) == len(actual)
        for item in files:
            relative = Path(item.filename.replace("\\", "/"))
            assert relative.parts[0] == package.name
            assert z.read(item) == (package / Path(*relative.parts[1:])).read_bytes()

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
                                         wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
    user32.SendMessageTimeoutW.restype = wintypes.LPARAM
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    env = os.environ.copy()
    windows_root = Path(os.environ["SYSTEMROOT"])
    env["PATH"] = str(package) + os.pathsep + str(windows_root / "System32") + os.pathsep + str(windows_root)
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "QML2_IMPORT_PATH"):
        env.pop(key, None)
    assert 1 <= args.launch_count <= 10
    launches = []
    for iteration in range(args.launch_count):
        with (work / f"gui-{iteration + 1}.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(client), "--host", manifest["server"], "--port", str(manifest["port"]),
                                        "--server-name", manifest["server"], "--ca", str(package / "server-cert.pem"),
                                        "--data-dir", str(work / "profile")] + (["--ui", args.ui] if args.ui else []), cwd=package, env=env, stdout=log, stderr=log)
            windows = []
    
            @callback_type
            def visit(window, _):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
                if pid.value == process.pid and user32.IsWindowVisible(window):
                    windows.append(window)
                return True
    
            try:
                deadline = time.monotonic() + 15
                while not windows and time.monotonic() < deadline:
                    assert process.poll() is None, "Packaged GUI exited before creating a window"
                    user32.EnumWindows(visit, 0)
                    time.sleep(0.1)
                assert windows, "No visible native GUI window"
                result = ctypes.c_size_t()
                assert user32.SendMessageTimeoutW(windows[0], 0, 0, 0, 2, 3000, ctypes.byref(result)), "GUI did not respond"
                time.sleep(0.5)
                command = ("$ErrorActionPreference='Stop'; ConvertTo-Json -Compress -InputObject "
                           "@(Get-NetTCPConnection -State Listen | Where-Object { $_.OwningProcess -eq "
                           + str(process.pid) + " } | Select-Object LocalAddress,LocalPort)")
                observed = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", command],
                                          capture_output=True, text=True, check=True,
                                          creationflags=subprocess.CREATE_NO_WINDOW)
                listeners = json.loads(observed.stdout)
                assert listeners == [], f"Unexpected startup listener: {listeners}"
                launches.append({"pid": process.pid, "tcp_listeners": listeners, "responsive": True})
                assert user32.PostMessageW(windows[0], 0x10, 0, 0), "Could not request normal GUI close"
                assert process.wait(timeout=8) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=8)
    report = {"passed": True, "payload_files": len(entries), "zip_verified": True,
              "client_matches_release_build": True, "file_transport": manifest["file_transport"],
              "certificate_sha256": fingerprint, "gui_visible": True, "gui_responded": True,
              "gui_exit_code": process.returncode, "startup_checks": launches, "path_contains_only_package_and_windows": True,
              "zip_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
    evidence.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
