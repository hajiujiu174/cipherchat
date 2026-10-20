"""Independently read the executable's PE resource; no installer execution."""
import ctypes
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
folder = root / 'artifacts/CipherChat-0.3.14.5-Setup'
exe = folder / 'setup.exe'
msi = folder / 'CipherChat-0.3.14.5-Windows-x64.msi'
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]; k.LoadLibraryExW.restype = w.HMODULE
k.FindResourceW.argtypes = [w.HMODULE, ctypes.c_void_p, ctypes.c_void_p]; k.FindResourceW.restype = w.HANDLE
k.LoadResource.argtypes = [w.HMODULE, w.HANDLE]; k.LoadResource.restype = w.HANDLE
k.LockResource.argtypes = [w.HANDLE]; k.LockResource.restype = ctypes.c_void_p
k.SizeofResource.argtypes = [w.HMODULE, w.HANDLE]; k.SizeofResource.restype = w.DWORD
k.FreeLibrary.argtypes = [w.HMODULE]
handle = k.LoadLibraryExW(str(exe), None, 2)
assert handle
try:
    resource = k.FindResourceW(handle, 101, 10); assert resource
    length = k.SizeofResource(handle, resource)
    pointer = k.LockResource(k.LoadResource(handle, resource)); assert pointer
    embedded = ctypes.string_at(pointer, length)
    assert embedded == msi.read_bytes()
finally:
    k.FreeLibrary(handle)
record = dict(embedded_msi_matches=True, embedded_bytes=length, exe_bytes=exe.stat().st_size, exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), msi_sha256=hashlib.sha256(embedded).hexdigest())
(folder / 'setup-verification.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
print(json.dumps(record, indent=2))
