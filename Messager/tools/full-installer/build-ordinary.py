"""Build the ordinary-chat / email-login release without changing the old installer."""
from pathlib import Path
import datetime,hashlib,json,shutil

root=Path(__file__).resolve().parents[2]
version='0.3.15.0'
package=root/f'artifacts/CipherChat-Ordinary-{version}-Windows'
package.mkdir(exist_ok=False)
old=root/'runtime-v0.3.14/.updates/versions/0.3.14.5'
new=package/'.updates/versions'/version
shutil.copytree(old,new)
shutil.copy2(root/'work/build/Messager.exe',new/'Messager.exe')
shutil.copy2(root/'runtime-v0.3.14/cipherchat-launcher.exe',package/'cipherchat-launcher.exe')
(package/'current.txt').write_text('.updates/versions/'+version,encoding='ascii')
(package/'启动CipherChat.bat').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nstart "" cipherchat-launcher.exe\r\n')
(package/'使用说明.txt').write_text('CipherChat 普通聊天版 '+version+'\n\n使用用户名或已绑定邮箱与密码登录，或在登录页选择“邮箱验证码登录”。\n包含科学计算器、应用广场及双人在线五子棋。\n不使用用户私钥、指纹或端到端加密；保留 TLS 和密码验证。\n需要配套 ordinary-v1 服务器；请在服务器升级完成后分发安装。\n旧聊天记录首次打开时迁入普通格式，原加密数据库另存为 .before-ordinary.bak，仅用于回退。\n旧加密文件未完成的传输需要重新发送。\n',encoding='utf-8')
release=json.loads((new/'release.json').read_text(encoding='utf-8-sig'))
release.update(version=version,sequence=max(release['sequence']+1,int(datetime.datetime.now().strftime('%Y%m%d%H%M'))),notes='普通聊天版：移除用户私钥、指纹验证及端到端加密；支持用户名或邮箱密码登录，新增独立邮箱验证码登录页。')
release.pop('archives',None)
block_size=release['block_size'];release['files']=[]
for file in sorted(new.rglob('*')):
    if not file.is_file() or file.name=='release.json':continue
    assert not file.is_symlink() and file.resolve().is_relative_to(new.resolve())
    assert file.suffix.lower() not in {'.db','.key'} and 'private' not in file.name.lower()
    data=file.read_bytes()
    release['files'].append(dict(path=file.relative_to(new).as_posix(),size=len(data),blocks=[dict(size=len(data[i:i+block_size]),sha256=hashlib.sha256(data[i:i+block_size]).hexdigest()) for i in range(0,len(data),block_size)]))
(new/'release.json').write_text(json.dumps(release,ensure_ascii=False,indent=2),encoding='utf-8')
source=(Path(__file__).with_name('build.py')).read_text(encoding='utf-8')
source=source.replace('0.3.14.5',version).replace('full-installer-20261020','ordinary-installer-20261020').replace('CipherChat-Gomoku-'+version+'-Windows','CipherChat-Ordinary-'+version+'-Windows')
source=source.replace('新服务器 8.160.172.25 · 应用广场、科学计算器、在线五子棋。','普通聊天 · 邮箱验证码登录 · 应用广场与在线五子棋。')
source=source.replace("builder = engine / 'build_msi.py'", "(engine/'make-icon.py').write_text("+repr("import shutil,sys\nshutil.copyfile("+repr(str(root/'resources/app.ico'))+",sys.argv[1])\n")+",encoding='utf-8')\nbuilder = engine / 'build_msi.py'")
source=source.replace("builder.write_text(s, encoding='utf-8')", "s=s.replace('端到端加密聊天','即时聊天')\nbuilder.write_text(s, encoding='utf-8')")
source=source.replace("subprocess.run([str(compiler / 'windres.exe')", "(WORK/'payload.rc').write_text((WORK/'payload.rc').read_text()+'\\n1 ICON "+'"'+str(root/'resources/app.ico').replace('\\','/')+'"'+"\\n')\nsubprocess.run([str(compiler / 'windres.exe')")
exec(compile(source,str(Path(__file__).with_name('build.py')),'exec'))
