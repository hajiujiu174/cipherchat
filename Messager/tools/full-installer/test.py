"""Real isolated Windows Installer lifecycle, without touching actual shortcuts/accounts."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import winreg

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/CipherChat-0.3.14.5-Setup'
MSI = OUT / 'CipherChat-0.3.14.5-Windows-x64.msi'
META = json.loads(MSI.with_suffix('.json').read_bytes())
PACKAGE = Path(META['source_package'])
WORK = Path(tempfile.mkdtemp(prefix='full-setup-test-', dir=ROOT / 'work'))
INSTALL = WORK / '全新安装 含空格' / 'CipherChat'
DESKTOP, MENU, DATA = [WORK / x for x in ['desktop', 'menu', 'accounts']]
for p in [DESKTOP, MENU, DATA]: p.mkdir()
(DATA / 'client.db').write_bytes(b'untouched account fixture')
api = ctypes.WinDLL('msi')
api.MsiQueryProductStateW.argtypes = [ctypes.c_wchar_p]
api.MsiQueryProductStateW.restype = ctypes.c_int
assert api.MsiQueryProductStateW(META['product_code']) == -1
key = r'Software\CipherChat\MessagerInstaller'
try:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key):
        raise RuntimeError('Existing installer settings found; refuse to modify them during tests')
except FileNotFoundError:
    pass
props = ['INSTALLDIR=' + str(INSTALL), 'MENUDIR=' + str(MENU), 'DESKTOPDIR=' + str(DESKTOP), 'DESKTOPSHORTCUT=1']
checks = []


def msi(mode, phase, extra=(), accepted=(0,)):
    cmd = subprocess.list2cmdline(['msiexec.exe', mode, str(MSI) if mode != '/x' else META['product_code'], '/qn', '/norestart', '/L*v', str(WORK / (phase + '.log'))])
    for arg in extra:
        name, val = arg.split('=', 1)
        assert '"' not in val
        cmd += ' ' + name + '="' + val + '"'
    code = subprocess.run(cmd, timeout=180).returncode
    assert code in accepted, (phase, code, WORK)


installed = False
try:
    msi('/i', 'install', props); installed = True
    assert api.MsiQueryProductStateW(META['product_code']) == 5
    for p in PACKAGE.rglob('*'):
        if p.is_file(): assert (INSTALL / p.relative_to(PACKAGE)).read_bytes() == p.read_bytes()
    checks.append('fresh installation: every payload file matches')
    assert len(list(DESKTOP.glob('*.lnk'))) == 1 and len(list(MENU.rglob('*.lnk'))) == 1
    checks.append('desktop and Start menu shortcuts')
    active = INSTALL / '.updates/versions/0.3.14.5'
    c = json.loads((active / 'connection.json').read_bytes())
    assert c['host'] == '8.160.172.25' and c['accountScope'] == '2603:1040:f02:d::21d:55220/2603:1040:f02:d::21d'
    checks.append('new server and original identity scope configured')
    # Launch installed code in demo capture mode under a clean dependency environment.
    capture = WORK / 'capture'
    env = dict(os.environ, PATH=os.environ['SYSTEMROOT'] + r'\System32')
    for keyname in ['QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QML2_IMPORT_PATH']:
        env.pop(keyname, None)
    subprocess.run([str(INSTALL / 'cipherchat-launcher.exe'), '--demo', '--capture', str(capture)], env=env, timeout=25, check=True)
    import time
    deadline = time.monotonic() + 65
    while not (capture / 'capture-check.json').exists() and time.monotonic() < deadline: time.sleep(.5)
    assert (capture / 'capture-check.json').exists()
    checks.append('installed launcher and Qt runtime execute demo capture without developer PATH')
    missing = active / 'platforms/qwindows.dll'
    expected = missing.read_bytes(); missing.unlink()
    msi('/fa', 'repair')
    assert missing.read_bytes() == expected
    assert (INSTALL / 'current.txt').read_text().strip() == '.updates/versions/0.3.14.5'
    checks.append('maintenance restores deleted runtime and remembers selected directories')
    (INSTALL / 'my-note.txt').write_text('preserve unrelated file')
    msi('/x', 'uninstall'); installed = False
    assert api.MsiQueryProductStateW(META['product_code']) == -1
    assert not (active / 'Messager.exe').exists()
    assert not list(DESKTOP.glob('*.lnk')) and not list(MENU.rglob('*.lnk'))
    assert (DATA / 'client.db').read_bytes() == b'untouched account fixture'
    assert (INSTALL / 'my-note.txt').read_text() == 'preserve unrelated file'
    checks.append('uninstall removes installed program and shortcuts, retains account fixture and unrelated file')
    msi('/i', 'no-desktop-install', [p for p in props if not p.startswith('DESKTOPSHORTCUT=')] + ['DESKTOPSHORTCUT=']); installed = True
    assert not list(DESKTOP.glob('*.lnk')) and len(list(MENU.rglob('*.lnk'))) == 1
    msi('/fa', 'no-desktop-repair')
    assert not list(DESKTOP.glob('*.lnk'))
    msi('/x', 'no-desktop-uninstall'); installed = False
    checks.append('optional desktop choice is preserved through repair')
    result = dict(passed=True, checks=checks, work=str(WORK), installer_sha256=hashlib.sha256((OUT / 'setup.exe').read_bytes()).hexdigest())
    (WORK / 'verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))
finally:
    if installed: msi('/x', 'cleanup')
