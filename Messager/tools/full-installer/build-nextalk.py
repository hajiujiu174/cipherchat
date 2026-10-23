"""Build the NEXTALK per-user installer with the existing verified MSI engine."""
from pathlib import Path
import importlib.util,hashlib,json,shutil,subprocess,os
ROOT=Path(__file__).resolve().parents[2];VERSION='0.3.25.0'
WORK=ROOT/'work/nextalk-brand-20261021/installer-build';OUT=ROOT/f'artifacts/NEXTALK-{VERSION}-Setup'
WORK.mkdir(exist_ok=False);OUT.mkdir(exist_ok=False)
package=WORK/'package';shutil.copytree(ROOT/f'artifacts/NEXTALK-{VERSION}-Windows',package)
(package/'release.json').write_text(json.dumps({'version':VERSION}),encoding='utf-8')
entries=[]
for p in sorted(package.rglob('*')):
 if p.is_file():
  assert p.suffix.lower() not in {'.db','.key','.sqlite','.sqlite3'}
  entries.append({'path':p.relative_to(package).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(package/'manifest.json').write_text(json.dumps({'files':entries}),encoding='utf-8')
engine=WORK/'engine';engine.mkdir();source=ROOT.parent/'cipherchat/scripts/installer'
t=(source/'build_msi.py').read_text(encoding='utf-8')
t=t.replace('91f73744-5e16-4784-a941-ae0ed794187c','908ebc82-cdd5-482c-a6b4-7d9ea8ae35d6')
t=t.replace('f"{year-2000}.{month}.{day*100+revision}"','f"{year}.{month}.{day*100+revision}"')
t=t.replace('CipherChat','NEXTALK').replace('NEXTALK-MSI-','CipherChat-MSI-')
t=t.replace(r'Software\NEXTALK\Installer',r'Software\CipherChat\MessagerInstaller')
t=t.replace('端到端加密聊天','即时聊天').replace('程序、加密库与增量更新组件','聊天、小游戏与增量更新组件').replace('NEXTALK 加密聊天','NEXTALK 即时聊天')
t=t.replace('"[INSTALLDIR]cipherchat-launcher.exe"','"[INSTALLDIR]NEXTALK.exe"')
t=t.replace('for p in sorted(expected)]', 'for p in sorted(expected) if p != "NEXTALK.ico"]')
(engine/'build_msi.py').write_text(t,encoding='utf-8')
a=(source/'actions.cpp').read_text(encoding='utf-8').replace('91f73744-5e16-4784-a941-ae0ed794187c','908ebc82-cdd5-482c-a6b4-7d9ea8ae35d6')
a=a.replace('if (!same(root.filename(), L"CipherChat"))', 'if (!same(root.filename(), L"CipherChat") && !same(root.filename(),L"NEXTALK"))')
a=a.replace('if (_wcsicmp(process.szExeFile, L"cipherchat.exe")', 'if (_wcsicmp(process.szExeFile,L"NEXTALK.exe") && _wcsicmp(process.szExeFile,L"NEXTALK-branding.exe") && _wcsicmp(process.szExeFile,L"Messager.exe") && _wcsicmp(process.szExeFile,L"CipherChat-UI-Selector.exe") && _wcsicmp(process.szExeFile, L"cipherchat.exe")')
a=a.replace('中的 CipherChat','中的 NEXTALK').replace('末级名称为 CipherChat','末级名称为 NEXTALK（旧安装可保留 CipherChat）')
(engine/'actions.cpp').write_text(a,encoding='utf-8')
(engine/'make-icon.py').write_text('import shutil,sys\nshutil.copyfile('+repr(str(ROOT/'resources/app.ico'))+',sys.argv[1])\n',encoding='utf-8')
spec=importlib.util.spec_from_file_location('nextalk_msi',engine/'build_msi.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
msi=OUT/f'NEXTALK-{VERSION}-Windows-x64.msi';module.build(package.resolve(),msi.resolve(),WORK/'msi')
bootstrap=(ROOT/'tools/full-installer/bootstrap.cpp').read_text(encoding='utf-8').replace('CipherChat','NEXTALK')
(WORK/'bootstrap.cpp').write_text(bootstrap,encoding='utf-8')
(WORK/'payload_hash.h').write_text('#define PAYLOAD_SHA256 "'+hashlib.sha256(msi.read_bytes()).hexdigest()+'"\n',encoding='utf-8')
rc='101 RCDATA "'+str(msi.resolve()).replace('\\','/')+'"\n1 24 "'+str(ROOT/'tools/server-setup/setup.manifest').replace('\\','/')+'"\n#include "'+str(ROOT/'resources/app.rc').replace('\\','/')+'"\n'
(WORK/'payload.rc').write_text(rc,encoding='utf-8')
compiler=Path('D:/msys64/mingw64/bin');env=dict(os.environ,PATH=str(compiler)+os.pathsep+os.environ['PATH'])
subprocess.run([str(compiler/'windres.exe'),'-I',str(ROOT/'resources'),str(WORK/'payload.rc'),str(WORK/'payload.o')],env=env,check=True)
subprocess.run([str(compiler/'g++.exe'),'-std=c++20','-O2','-s','-static','-municode','-mwindows','-I'+str(WORK),str(WORK/'bootstrap.cpp'),str(WORK/'payload.o'),'-o',str(OUT/'setup.exe'),'-lmsi','-lbcrypt','-lole32'],env=env,check=True)
print(json.dumps({'setup':str(OUT/'setup.exe'),'sha256':hashlib.sha256((OUT/'setup.exe').read_bytes()).hexdigest(),'msi':str(msi)}),flush=True)
