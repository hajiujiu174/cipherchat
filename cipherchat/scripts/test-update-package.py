"""Isolated Windows upgrade/rollback of real release payloads using the public center."""
import argparse
import ctypes
from ctypes import wintypes as w
import importlib.machinery
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

u = ctypes.WinDLL("user32", use_last_error=True)
k = ctypes.WinDLL("kernel32", use_last_error=True)
callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
u.IsWindowVisible.argtypes = [w.HWND]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
k.CloseHandle.argtypes = [w.HANDLE]
k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, ctypes.POINTER(ctypes.c_size_t)]

def find(exe):
    found = []
    @callback
    def visit(window, _):
        if not u.IsWindowVisible(window): return True
        pid = w.DWORD(); u.GetWindowThreadProcessId(window, ctypes.byref(pid))
        process = k.OpenProcess(0x1000, False, pid.value)
        if process:
            try:
                length = w.DWORD(32768); name = ctypes.create_unicode_buffer(length.value)
                if k.QueryFullProcessImageNameW(process, 0, name, ctypes.byref(length)) and Path(name.value) == exe:
                    found.append((window, pid.value))
            finally: k.CloseHandle(process)
        return True
    u.EnumWindows(visit, 0)
    return found

def wait_window(exe):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        found = find(exe)
        if found:
            result = ctypes.c_size_t()
            assert u.SendMessageTimeoutW(found[0][0], 0, 0, 0, 2, 3000, ctypes.byref(result))
            return found[0]
        time.sleep(.1)
    raise TimeoutError("No responsive test window for " + str(exe))

def close_window(window, pid):
    handle = k.OpenProcess(0x100000, False, pid)
    try:
        assert u.PostMessageW(window, 0x10, 0, 0)
        if handle: assert k.WaitForSingleObject(handle, 15000) == 0
    finally:
        if handle: k.CloseHandle(handle)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("package", type=Path); p.add_argument("old_binary", type=Path); p.add_argument("work", type=Path)
    a = p.parse_args(); package = a.package.resolve(); work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    root = work / "upgrade with spaces"; shutil.copytree(package, root)
    shutil.copyfile(a.old_binary, root / "cipherchat.exe")
    build = importlib.machinery.SourceFileLoader("feed", str(Path(__file__).with_name("build-update-feed.py"))).load_module()
    build.build(root, work / "fixture-feed", "2026.9.19.0", 2026091900, "Local test fixture containing the previous GUI binary")
    profile = work / "isolated profile"; profile.mkdir(); marker = profile / "keep.txt"; marker.write_text("user data stays")
    system = Path(os.environ["SYSTEMROOT"])
    env = dict(os.environ, PATH=str(root) + os.pathsep + str(system / "System32") + os.pathsep + str(system))
    for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "QML2_IMPORT_PATH"): env.pop(name, None)
    helper = root / "cipherchat-updater.exe"
    config = json.loads((root / "update-config.json").read_bytes())
    args = ["--data-dir", str(profile), "--host", config["host"], "--port", str(config["port"]), "--server-name", config["server_name"], "--ca", str(root / "server-cert.pem")]
    stage = subprocess.run([str(helper), "--stage", str(root)], env=env, cwd=root, capture_output=True, timeout=600)
    (work / "stage.log").write_bytes(stage.stdout + stage.stderr)
    assert stage.returncode == 0, stage.stdout.decode("utf-8", "replace")[-1000:]
    result = json.loads(stage.stdout.splitlines()[-1]); assert result["event"] == "staged" and result["reused"] > result["downloaded"] > 0
    current = root / ".updates" / "versions" / result["version"]
    active_window = None
    try:
        # Launcher starts the genuine prior GUI, with a separate profile.
        subprocess.run([str(root / "cipherchat-launcher.exe"), *args], env=env, cwd=root, check=True, timeout=10)
        active_window = wait_window(root / "cipherchat.exe")
        def switch(mode, running, version=None):
            window, pid = running
            (root / ".updates" / ("restart-" + str(pid) + ".json")).write_text(json.dumps(args), encoding="utf-8")
            command = [str(helper), mode, str(root)] + ([version] if version else []) + ["--wait-pid", str(pid)]
            update = subprocess.Popen(command, env=env, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(.2)
            assert update.poll() is None, "Updater must wait for the running client"
            close_window(window, pid)
            out, err = update.communicate(timeout=45)
            (work / (mode[2:] + ".log")).write_bytes(out + err)
            assert update.returncode == 0, out
        switch("--activate", active_window, result["version"]); active_window = None
        assert (root / "current.txt").read_text() == ".updates/versions/" + result["version"]
        active_window = wait_window(current / "cipherchat.exe")
        switch("--rollback", active_window); active_window = None
        assert (root / "current.txt").read_text() == "."
        active_window = wait_window(root / "cipherchat.exe")
        close_window(*active_window); active_window = None
        assert marker.read_text() == "user data stays"
        for item in json.loads((current / "release.json").read_bytes())["files"]:
            assert (current / item["path"]).read_bytes() == (package / item["path"]).read_bytes()
        report = dict(passed=True, public_delta=result, activated_and_relaunched=True, rollback_and_relaunched=True, clean_windows_path=True, data_preserved=True, payload_matches_published_package=True, fixture="previous appearance GUI plus new updater; local-only baseline 2026.9.19.0")
        (work / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(report))
    finally:
        for exe in (root / "cipherchat.exe", current / "cipherchat.exe"):
            for window, pid in find(exe): close_window(window, pid)

if __name__ == "__main__": main()
