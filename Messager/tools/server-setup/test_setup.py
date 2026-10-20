"""Run the shipped executable against disposable installations, never real accounts."""
import ctypes
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'artifacts/server-switch-20261020/setup.exe'
WORK = Path(tempfile.mkdtemp(prefix='setup-', dir=ROOT / 'work'))
OLD = '2603:1040:f02:d::21d'
NEW = '8.160.172.25'
passed = []


def fixture(name):
    d = WORK / name
    d.mkdir()
    (d / 'Messager.exe').write_bytes(b'fixture-program')
    (d / 'connection.json').write_text(json.dumps(dict(host=OLD, port=55220, serverName=OLD, dataDirectory='keep-data', username='fixture-user', custom='retain')), encoding='utf-8')
    (d / 'update-config.json').write_text(json.dumps(dict(host=OLD, port=55220, server_name=OLD, channel='messager')), encoding='utf-8')
    (d / 'client.db').write_bytes(b'private-fixture-data')
    (d / 'server-cert.pem').write_bytes(b'certificate-fixture')
    return d


def run(op, path, ok=True):
    report = WORK / ('report-' + str(len(list(WORK.glob('report-*')))) + '.txt')
    p = subprocess.run([str(EXE), op, str(path), '--report', str(report)], timeout=25)
    message = report.read_text(encoding='utf-8')
    assert (p.returncode == 0) == ok, (p.returncode, message)
    return message


def snapshot(d):
    return {p.name: p.read_bytes() for p in d.iterdir() if p.is_file()}


d = fixture('普通安装-中文路径')
before = snapshot(d)
backup = Path(run('--apply', d))
c = json.loads((d / 'connection.json').read_text())
assert c['host'] == NEW and c['accountScope'] == OLD + ':55220/' + OLD
assert c['dataDirectory'] == 'keep-data' and c['username'] == 'fixture-user' and c['custom'] == 'retain'
assert json.loads((d / 'update-config.json').read_text())['channel'] == 'messager'
assert (d / 'client.db').read_bytes() == before['client.db']
assert (d / 'server-cert.pem').read_bytes() == before['server-cert.pem']
passed.append('plain migration preserves data, certificate, extra fields and original account scope')
run('--apply', d)
assert json.loads((d / 'connection.json').read_text())['accountScope'] == c['accountScope']
passed.append('repeat migration preserves scope')
run('--restore', backup)
assert snapshot(d) == before
passed.append('exact byte restoration')

r = fixture('launcher')
active = r / '.updates/versions/0.3.14.5'
active.mkdir(parents=True)
for p in r.iterdir():
    if p.is_file(): (active / p.name).write_bytes(p.read_bytes())
(r / 'current.txt').write_text('.updates/versions/0.3.14.5')
s = WORK / 'selector'; s.mkdir()
(s / 'desktop-frontends.json').write_text(json.dumps(dict(frontends=[dict(id='messager', program='../launcher/cipherchat-launcher.exe'), dict(id='rhine', program='../absent/unknown.exe')])))
run('--apply', s)
assert all(json.loads((p / 'connection.json').read_text())['host'] == NEW for p in [r, active])
assert (r / 'current.txt').read_text() == '.updates/versions/0.3.14.5'
passed.append('selector, nested active version and root migrated, other frontend untouched')

r = fixture('bad-pointer'); (r / 'current.txt').write_text('../outside')
before = snapshot(r); run('--apply', r, False); assert snapshot(r) == before
passed.append('unsafe pointer rejected without changes')
r = fixture('custom-host'); o = json.loads((r / 'connection.json').read_text()); o['host'] = 'custom.example'; (r / 'connection.json').write_text(json.dumps(o))
before = snapshot(r); run('--apply', r, False); assert snapshot(r) == before
passed.append('custom server rejected without changes')
r = fixture('broken-update'); (r / 'update-config.json').write_text('{broken')
before = snapshot(r); run('--apply', r, False); assert snapshot(r) == before
passed.append('all configs parsed before any mutation')
r = fixture('custom-scope'); o = json.loads((r / 'connection.json').read_text()); o['accountScope'] = 'preserve-this'; (r / 'connection.json').write_text(json.dumps(o)); run('--apply', r)
assert json.loads((r / 'connection.json').read_text())['accountScope'] == 'preserve-this'
passed.append('explicit scope retained')

r = fixture('locked-update'); before = snapshot(r)
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateFileW.restype = ctypes.c_void_p
kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
kernel.CloseHandle.argtypes = [ctypes.c_void_p]
h = kernel.CreateFileW(str(r / 'update-config.json'), 0x80000000, 1, None, 3, 0, None)
assert h != ctypes.c_void_p(-1).value
try:
    run('--apply', r, False)
    assert snapshot(r) == before
finally:
    kernel.CloseHandle(h)
passed.append('mid-transaction locked file triggers exact rollback')

# A real separate process with the client's name proves the no-force-close guard.
r = fixture('running-client')
cs = WORK / 'dummy.cs'; cs.write_text('class Dummy { static void Main() { System.Threading.Thread.Sleep(60000); } }')
subprocess.run([r'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe', '/nologo', '/target:winexe', '/out:' + str(r / 'Messager.exe'), str(cs)], check=True)
before = snapshot(r)
p = subprocess.Popen([str(r / 'Messager.exe')])
try:
    run('--apply', r, False)
    assert p.poll() is None and snapshot(r) == before
finally:
    p.terminate(); p.wait(timeout=10)
passed.append('running client refused without termination or writes')

result = dict(passed=len(passed), checks=passed, work=str(WORK), sha256=hashlib.sha256(EXE.read_bytes()).hexdigest())
(WORK / 'verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=False, indent=2))
