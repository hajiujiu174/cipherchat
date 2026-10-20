"""Install/repair/uninstall the actual MSI in an isolated workspace; close only test windows."""
import argparse
import ctypes
import hashlib
import importlib.machinery
import json
import os
from pathlib import Path
import shutil
import subprocess
import winreg

def main(installer, work):
    metadata = json.loads(installer.with_suffix(".json").read_bytes())
    assert hashlib.sha256(installer.read_bytes()).hexdigest() == metadata["sha256"]
    product = metadata["product_code"]
    api = ctypes.WinDLL("msi"); api.MsiQueryProductStateW.argtypes = [ctypes.c_wchar_p]; api.MsiQueryProductStateW.restype = ctypes.c_int
    assert api.MsiQueryProductStateW(product) == -1, "Existing installation found; do not modify it"
    package = Path(metadata["source_package"])
    work.mkdir(parents=True, exist_ok=False)
    install = work / "含空格 programs" / "CipherChat"
    menu = work / "menu"; desktop = work / "desktop"; profile = work / "user data"
    for path in (menu, desktop, profile): path.mkdir()
    (profile / "keep.txt").write_text("Keep account and chat data", encoding="utf-8")
    properties = ["INSTALLDIR=" + str(install), "MENUDIR=" + str(menu), "DESKTOPDIR=" + str(desktop), "DESKTOPSHORTCUT=1"]
    scripts = Path(__file__).resolve().parent.parent
    windows = importlib.machinery.SourceFileLoader("package_windows", str(scripts / "test-update-package.py")).load_module()
    installed = False
    def msi(mode, phase, extra=(), accepted=(0,)):
        command = subprocess.list2cmdline(["msiexec.exe", mode, str(installer) if mode != "/x" else product, "/qn", "/norestart", "/L*v", str(work / (phase + ".log"))])
        for item in extra:
            key, value = item.split("=", 1); assert '"' not in value
            command += ' ' + key + '="' + value.rstrip('\\') + '"'
        result = subprocess.run(command, timeout=120)
        assert result.returncode in accepted, (phase, result.returncode, str(work / (phase + ".log")))
    try:
        msi("/a", "extract", ["TARGETDIR=" + str(work / "extracted")])
        extracted = list((work / "extracted").rglob("cipherchat.exe")); assert len(extracted) == 1
        for source in package.rglob("*"):
            if source.is_file(): assert (extracted[0].parent / source.relative_to(package)).read_bytes() == source.read_bytes()
        msi("/i", "install", properties); installed = True
        assert api.MsiQueryProductStateW(product) == 5
        for source in package.rglob("*"):
            if source.is_file(): assert (install / source.relative_to(package)).read_bytes() == source.read_bytes()
        assert (install / ".cipherchat-msi-owner").read_text() == "CipherChat-MSI-91f73744-5e16-4784-a941-ae0ed794187c\n"
        shortcuts = list(menu.rglob("*.lnk")) + list(desktop.rglob("*.lnk")); assert len(shortcuts) == 2
        for shortcut in shortcuts:
            code = "[Console]::OutputEncoding=[Text.UTF8Encoding]::new(); $link=(New-Object -ComObject WScript.Shell).CreateShortcut($env:CC_TEST_SHORTCUT); $link.TargetPath"
            result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", code], capture_output=True, text=True, encoding="utf-8", env=dict(os.environ, CC_TEST_SHORTCUT=str(shortcut), PYTHONIOENCODING="utf-8"))
            # Reading a .lnk does not save or modify it.
            assert result.returncode == 0 and Path(result.stdout.strip()) == install / "cipherchat-launcher.exe", result.stderr
        api.MsiGetProductInfoW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
        def product_info(name):
            size = ctypes.c_ulong(32768); value = ctypes.create_unicode_buffer(size.value)
            assert api.MsiGetProductInfoW(product, name, value, ctypes.byref(size)) == 0
            return value.value
        assert metadata["version"] in product_info("InstalledProductName")
        assert product_info("AssignmentType") == "0"  # Windows Installer current-user registration.
        assert Path(product_info("InstallLocation")) == install
        env = dict(os.environ, PATH=str(install) + os.pathsep + str(Path(os.environ["SYSTEMROOT"]) / "System32"))
        for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "QML2_IMPORT_PATH"): env.pop(name, None)
        launch_args = ["--data-dir", str(profile)]
        subprocess.run([str(install / "cipherchat-launcher.exe"), *launch_args], env=env, check=True, timeout=15)
        window, pid = windows.wait_window(install / "cipherchat.exe")
        # Running program must be preserved; no forced shutdown by the installer.
        msi("/x", "running-refused", properties, accepted=(1603,))
        assert windows.find(install / "cipherchat.exe") and (install / "cipherchat.exe").is_file()
        windows.close_window(window, pid)
        update = subprocess.run([str(install / "cipherchat-updater.exe"), "--check", str(install)], capture_output=True, timeout=120, env=env)
        (work / "update-check.log").write_bytes(update.stdout + update.stderr)
        assert update.returncode == 0
        check = json.loads(update.stdout.splitlines()[-1]); assert check["event"] == "checked" and check["current"] == metadata["version"]
        # Exercise the dynamic version layout that MSI must preserve during repair and remove on uninstall.
        active = install / ".updates" / "versions" / metadata["version"]; shutil.copytree(package, active)
        (install / "current.txt").write_text(".updates/versions/" + metadata["version"], encoding="ascii")
        cache = install / ".updates" / "cache"; cache.mkdir(); (cache / "fixture").write_bytes(b"update-cache")
        missing = install / "styles" / "qmodernwindowsstyle.dll"
        assert missing.resolve().is_relative_to(install.resolve()); missing.unlink()
        msi("/fa", "repair")  # Maintenance must recover directories without command-line overrides.
        assert missing.read_bytes() == (package / "styles" / missing.name).read_bytes()
        assert (active / "cipherchat.exe").is_file() and (install / "current.txt").exists()
        subprocess.run([str(install / "cipherchat-launcher.exe"), *launch_args], env=env, check=True, timeout=15)
        windows.close_window(*windows.wait_window(active / "cipherchat.exe"))
        # An unrelated user-created file inside the program directory is not recursively erased.
        (install / "user-note.txt").write_text("keep this unrelated file")
        msi("/x", "uninstall"); installed = False
        assert api.MsiQueryProductStateW(product) == -1
        assert not (install / "cipherchat.exe").exists() and not (install / ".updates").exists()
        assert not (install / "current.txt").exists() and not any(menu.rglob("*.lnk")) and not any(desktop.rglob("*.lnk"))
        assert (profile / "keep.txt").read_text() == "Keep account and chat data"
        assert (install / "user-note.txt").read_text() == "keep this unrelated file"
        # Reinstall without the optional desktop shortcut.
        no_desktop = [p for p in properties if not p.startswith("DESKTOPSHORTCUT=")] + ["DESKTOPSHORTCUT="]
        msi("/i", "no-desktop-install", no_desktop); installed = True
        assert len(list(menu.rglob("*.lnk"))) == 1 and not any(desktop.rglob("*.lnk"))
        msi("/fa", "no-desktop-repair")
        assert len(list(menu.rglob("*.lnk"))) == 1 and not any(desktop.rglob("*.lnk"))
        msi("/x", "no-desktop-uninstall"); installed = False
        assert not any(menu.rglob("*.lnk")) and not any(desktop.rglob("*.lnk"))
        report = dict(passed=True, version=metadata["version"], admin_extract_matches=True, install_matches=True, per_user_registration=True,
                      shortcuts=True, launch=True, refuses_running_uninstall=True, update_check=check, repair=True, version_pointer_preserved=True,
                      staged_version_launch=True, uninstall_updates_removed=True, unrelated_file_preserved=True, data_preserved=True, optional_desktop=True,
                      remembered_directories=True, arp_install_location=True, desktop_choice_preserved=True,
                      installation_path=str(install), installer=str(installer), sha256=metadata["sha256"])
        (work / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8"); print(json.dumps(report))
    finally:
        for exe in install.rglob("cipherchat.exe") if install.exists() else []:
            for window, pid in windows.find(exe): windows.close_window(window, pid)
        if installed:
            msi("/x", "test-cleanup", properties)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", type=Path, required=True); parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(); main(args.installer.resolve(), args.work.resolve())
