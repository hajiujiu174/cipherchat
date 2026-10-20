"""Reuse the project's tested current-user MSI engine with a separate Messager identity."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT.parent / 'cipherchat/scripts/installer'
WORK = ROOT / 'work/full-installer-20261020'
OUTPUT = ROOT / 'artifacts/CipherChat-0.3.14.5-Setup'
WORK.mkdir(exist_ok=False)
OUTPUT.mkdir(exist_ok=False)
package = WORK / 'package'
shutil.copytree(ROOT / 'artifacts/CipherChat-Gomoku-0.3.14.5-Windows', package)
# An MSI packaging manifest is separate from the nested client's update release manifest.
(package / 'release.json').write_text(json.dumps(dict(version='0.3.14.5')), encoding='utf-8')
entries = []
for p in sorted(package.rglob('*')):
    if p.is_file():
        rel = p.relative_to(package).as_posix()
        assert p.suffix.lower() not in {'.db', '.key'} and 'private' not in p.name.lower()
        entries.append(dict(path=rel, bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(package / 'manifest.json').write_text(json.dumps(dict(files=entries)), encoding='utf-8')
engine = WORK / 'engine'; engine.mkdir()
for name in ['build_msi.py', 'actions.cpp', 'make-icon.py']:
    shutil.copy2(SOURCE / name, engine / name)
builder = engine / 'build_msi.py'
s = builder.read_text(encoding='utf-8')
def change(old, new):
    global s
    assert old in s, old
    s = s.replace(old, new)
change('f"{year-2000}.{month}.{day*100+revision}"', 'f"{year}.{month}.{day*100+revision}"')
change('91f73744-5e16-4784-a941-ae0ed794187c', '908ebc82-cdd5-482c-a6b4-7d9ea8ae35d6')
change('Software\\CipherChat\\Installer', 'Software\\CipherChat\\MessagerInstaller')
change('("INSTALLDIR", "ProgramsDir", "CipherChat")', '("MessagerProgramsDir", "ProgramsDir", "CipherChat-Messager"), ("INSTALLDIR", "MessagerProgramsDir", "CipherChat")')
change('("MENUDIR", "ProgramMenuFolder", "CipherChat")', '("MENUDIR", "ProgramMenuFolder", "CipherChat Messager")')
change('私密聊天、文件传输、好友申请及软件内增量更新。', '新服务器 8.160.172.25 · 应用广场、科学计算器、在线五子棋。')
builder.write_text(s, encoding='utf-8')
actions = engine / 'actions.cpp'
s = actions.read_text(encoding='utf-8').replace('91f73744-5e16-4784-a941-ae0ed794187c', '908ebc82-cdd5-482c-a6b4-7d9ea8ae35d6')
s = s.replace('if (_wcsicmp(process.szExeFile, L"cipherchat.exe")', 'if (_wcsicmp(process.szExeFile, L"Messager.exe") && _wcsicmp(process.szExeFile, L"CipherChat-UI-Selector.exe") && _wcsicmp(process.szExeFile, L"cipherchat.exe")')
actions.write_text(s, encoding='utf-8')
spec = importlib.util.spec_from_file_location('messager_msi', builder)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
msi = OUTPUT / 'CipherChat-0.3.14.5-Windows-x64.msi'
module.build(package.resolve(), msi.resolve(), WORK / 'msi')
# Embed the complete MSI into a dependency-free native bootstrapper.
digest = hashlib.sha256(msi.read_bytes()).hexdigest()
(WORK / 'payload_hash.h').write_text('#define PAYLOAD_SHA256 "' + digest + '"\n')
(WORK / 'payload.rc').write_text('101 RCDATA "' + str(msi.resolve()).replace('\\', '/') + '"\n1 24 "' + str(ROOT / 'tools/server-setup/setup.manifest').replace('\\', '/') + '"\n')
compiler = Path('D:/msys64/mingw64/bin')
env = dict(os.environ, PATH=str(compiler) + os.pathsep + os.environ['PATH'])
subprocess.run([str(compiler / 'windres.exe'), str(WORK / 'payload.rc'), str(WORK / 'payload.o')], check=True, env=env)
subprocess.run([str(compiler / 'g++.exe'), '-std=c++20', '-O2', '-s', '-static', '-municode', '-mwindows', '-I' + str(WORK), str(ROOT / 'tools/full-installer/bootstrap.cpp'), str(WORK / 'payload.o'), '-o', str(OUTPUT / 'setup.exe'), '-lmsi', '-lbcrypt', '-lole32'], check=True, env=env)
print(json.dumps(dict(exe=str(OUTPUT / 'setup.exe'), bytes=(OUTPUT / 'setup.exe').stat().st_size, sha256=hashlib.sha256((OUTPUT / 'setup.exe').read_bytes()).hexdigest()), indent=2))
