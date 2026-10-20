"""Verify only the native MSI windows launched by this test; no global desktop input."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import hashlib
import os
from pathlib import Path
import subprocess
import time
from PIL import Image

u = c.WinDLL("user32"); g = c.WinDLL("gdi32")
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]; u.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.IsWindowVisible.argtypes = [w.HWND]; u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetDlgCtrlID.argtypes = [w.HWND]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetDC.argtypes = [w.HWND]; u.GetDC.restype = w.HDC
u.ReleaseDC.argtypes = [w.HWND, w.HDC]
u.PrintWindow.argtypes = [w.HWND, w.HDC, w.UINT]
g.CreateCompatibleDC.argtypes = [w.HDC]; g.CreateCompatibleDC.restype = w.HDC
g.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]; g.CreateCompatibleBitmap.restype = w.HBITMAP
g.SelectObject.argtypes = [w.HDC, w.HGDIOBJ]; g.SelectObject.restype = w.HGDIOBJ
g.DeleteObject.argtypes = [w.HGDIOBJ]; g.DeleteDC.argtypes = [w.HDC]
class Header(c.Structure):
    _fields_ = [("size",w.DWORD),("width",w.LONG),("height",w.LONG),("planes",w.WORD),("bits",w.WORD),("compression",w.DWORD),("imageSize",w.DWORD),("x",w.LONG),("y",w.LONG),("used",w.DWORD),("important",w.DWORD)]
g.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.POINTER(Header), w.UINT]
u.SetProcessDpiAwarenessContext.argtypes = [c.c_void_p]; u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
def title(handle):
    text = c.create_unicode_buffer(4096); u.GetWindowTextW(handle, text, len(text)); return text.value
def windows(pid):
    found = []
    @callback
    def visit(handle, _):
        process = w.DWORD(); u.GetWindowThreadProcessId(handle, c.byref(process))
        if process.value == pid and u.IsWindowVisible(handle): found.append(handle)
        return True
    u.EnumWindows(visit, 0); return found
def children(handle):
    found = []
    @callback
    def visit(child, _):
        if u.IsWindowVisible(child): found.append((child, title(child)))
        return True
    u.EnumChildWindows(handle, visit, 0); return found
def wait(pid, text):
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        for window in windows(pid):
            items = children(window)
            if any(label == text for _, label in items): return window, items
        time.sleep(.1)
    raise TimeoutError({title(win):children(win) for win in windows(pid)})
def capture(handle, output):
    rect = w.RECT(); u.GetWindowRect(handle, c.byref(rect)); width, height = rect.right-rect.left, rect.bottom-rect.top
    dc = u.GetDC(handle); buffer = g.CreateCompatibleDC(dc); bitmap = g.CreateCompatibleBitmap(dc, width, height); previous = g.SelectObject(buffer, bitmap)
    try:
        assert u.PrintWindow(handle, buffer, 2)
        data = c.create_string_buffer(width*height*4); header = Header(c.sizeof(Header), width, -height, 1, 32, 0, len(data), 0, 0, 0, 0)
        assert g.GetDIBits(buffer, bitmap, 0, height, data, c.byref(header), 0) == height
        Image.frombytes("RGB", (width,height), data.raw, "raw", "BGRX").save(output)
    finally:
        g.SelectObject(buffer, previous); g.DeleteObject(bitmap); g.DeleteDC(buffer); u.ReleaseDC(handle, dc)
def click(items, text):
    found = [handle for handle, label in items if label == text]; assert len(found) == 1
    u.SendMessageW(found[0], 0xF5, 0, 0)  # BM_CLICK on a discovered control in this test's process.
def maintenance(installer, work, default_location=False):
    install = Path(os.environ["LOCALAPPDATA"]) / "Programs" / "CipherChat" if default_location else work / "CipherChat"
    assert (install / ".cipherchat-msi-owner").read_text() == "CipherChat-MSI-91f73744-5e16-4784-a941-ae0ed794187c\n"
    child = subprocess.Popen(subprocess.list2cmdline(["msiexec.exe", "/i", str(installer), "/qf"]))
    maintenance, items = wait(child.pid, "卸载"); capture(maintenance, work / "maintenance.png"); click(items, "卸载")
    complete, items = wait(child.pid, "完成"); capture(complete, work / "uninstalled.png"); assert any("操作已完成" in label for _, label in items), items
    click(items, "完成"); assert child.wait(timeout=20) == 0
    assert not (install / "cipherchat.exe").exists() and not any((work / "menu").rglob("*.lnk")) and not any((work / "desktop").rglob("*.lnk"))
def main(installer, work, default_location=False):
    metadata = json.loads(installer.with_suffix('.json').read_bytes())
    assert hashlib.sha256(installer.read_bytes()).hexdigest() == metadata['sha256']
    api = c.WinDLL('msi'); api.MsiQueryProductStateW.argtypes = [w.LPCWSTR]; api.MsiQueryProductStateW.restype = c.c_int
    assert api.MsiQueryProductStateW(metadata['product_code']) == -1, 'Do not modify an existing installation'
    work.mkdir(parents=True, exist_ok=False)
    install = Path(os.environ["LOCALAPPDATA"]) / "Programs" / "CipherChat" if default_location else work / "CipherChat"
    assert not install.exists(), "Do not modify an existing user installation"
    command = subprocess.list2cmdline(["msiexec.exe", "/i", str(installer), "/qf"])
    if not default_location: command += f' INSTALLDIR="{install}"'
    command += f' MENUDIR="{work / "menu"}" DESKTOPDIR="{work / "desktop"}"'
    child = subprocess.Popen(command)
    welcome, items = wait(child.pid, "安装"); capture(welcome, work / "welcome.png")
    assert any(str(install) in label for _, label in items)
    click(items, "安装")
    complete, items = wait(child.pid, "完成"); capture(complete, work / "complete.png")
    assert any("操作已完成" in label for _, label in items), items
    click(items, "完成"); assert child.wait(timeout=20) == 0
    assert (install / "cipherchat.exe").is_file()
    # A bare maintenance invocation must recover the previous installation directory.
    maintenance(installer, work, default_location)
    assert api.MsiQueryProductStateW(metadata['product_code']) == -1
    # A rejected path exercises the built-in error dialog which previously produced error 2868.
    invalid = work / 'invalid-destination'
    command = subprocess.list2cmdline(['msiexec.exe', '/i', str(installer), '/qf']) + f' INSTALLDIR="{invalid}"'
    child = subprocess.Popen(command)
    _, items = wait(child.pid, '安装'); click(items, '安装')
    expected = '安装目录须为本地普通文件夹，末级名称为 CipherChat，且不能位于账号数据目录或链接目录中。'
    error, items = wait(child.pid, expected); capture(error, work / 'expected-error.png')
    click(items, '确定')
    failed, items = wait(child.pid, '完成'); capture(failed, work / 'failure-page.png')
    assert any('操作未完成' in label for _, label in items), items
    click(items, '完成'); assert child.wait(timeout=20) == 1603
    assert not invalid.exists() and api.MsiQueryProductStateW(metadata['product_code']) == -1
    (work / "result.json").write_text(json.dumps(dict(native_install_ui=True, maintenance_without_overrides=True, uninstall_ui=True,
        invalid_path_error_ui=True, registration_removed=True, installer=str(installer), sha256=metadata['sha256']), indent=2))
    print("Native MSI install and maintenance UI passed.")
if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("installer",type=Path); parser.add_argument("work",type=Path)
    parser.add_argument("--maintenance-only",action="store_true")
    parser.add_argument("--default-location",action="store_true")
    args=parser.parse_args(); (maintenance if args.maintenance_only else main)(args.installer.resolve(),args.work.resolve(),args.default_location)
