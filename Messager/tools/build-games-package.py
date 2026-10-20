"""Build a portable games preview from the current ordinary-chat runtime."""
from pathlib import Path, PurePosixPath
import argparse,datetime,hashlib,json,shutil,tempfile,zipfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',type=Path,default=root/'runtime-v0.3.14/.updates/versions/0.3.15.0')
args=parser.parse_args()
version='0.3.16.1'
package=root/f'artifacts/CipherChat-Games-{version}-Windows'
package.mkdir(exist_ok=False)
payload=package/'.updates/versions'/version
shutil.copytree(args.baseline,payload)
shutil.copy2(root/'work/build/Messager.exe',payload/'Messager.exe')
shutil.copy2(root/'runtime-v0.3.14/cipherchat-launcher.exe',package/'cipherchat-launcher.exe')
(package/'current.txt').write_text('.updates/versions/'+version,encoding='ascii')
(package/'启动CipherChat.bat').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nstart "" cipherchat-launcher.exe\r\n')
for game in ['connectfour','reversi']:
    target=payload/'licenses'/game;target.mkdir(exist_ok=False)
    for name in ['LICENSE','SOURCE.json','README.md']:shutil.copy2(root/'third_party'/game/name,target/name)
with (payload/'licenses/THIRD_PARTY_NOTICES.txt').open('a',encoding='utf-8') as stream:
    stream.write('\nFour in a row: Dev Share Academy, MIT; see connectfour/.\nReversi: Kaahan Motwani, MIT; see reversi/.\n')
shutil.copy2(root/'docs/ONLINE-GAMES.md',payload/'ONLINE-GAMES.md')
(package/'使用说明.txt').write_text('CipherChat '+version+' 联机小游戏预览版\n\n完整解压后运行“启动CipherChat.bat”。小程序中新增四子棋、黑白棋。\n选择好友创建房间，对方在聊天中点击“加入房间”。双方均需此版；请将整份 ZIP 发给好友。\n仍连接现有普通聊天服务器，无需升级服务器；本预览版尚未发布到在线更新通道。\n支持断线后重登恢复对局、重新同步、退出认输。黑白棋自动跳过无合法落子的玩家。\n修复旧加密文件回执重复发送导致 queue_rejected 刷屏：已完成旧文件作为只读历史保留，自动备份后清理对应网络回执。\n原有聊天、邮箱登录和五子棋保留。账号和历史目录沿用既有配置，不包含任何账号数据。\n开源项目、固定版本和许可详见 licenses/connectfour 与 licenses/reversi。\n',encoding='utf-8')
release=json.loads((payload/'release.json').read_bytes())
release.update(version=version,sequence=max(release['sequence']+1,int(datetime.datetime.now().strftime('%Y%m%d%H%M'))),notes='新增四子棋和黑白棋好友在线对战，支持聊天邀请、断线恢复与完整胜负判定。')
release.pop('archives',None);release['files']=[]
size=release['block_size']
for p in sorted(payload.rglob('*')):
    if not p.is_file() or p.name=='release.json':continue
    assert not p.is_symlink() and p.resolve().is_relative_to(payload.resolve())
    assert p.suffix.lower() not in {'.db','.key','.sqlite','.sqlite3'} and 'private' not in p.name.lower()
    data=p.read_bytes()
    release['files'].append(dict(path=p.relative_to(payload).as_posix(),size=len(data),blocks=[dict(size=len(data[i:i+size]),sha256=hashlib.sha256(data[i:i+size]).hexdigest()) for i in range(0,len(data),size)]))
(payload/'release.json').write_text(json.dumps(release,ensure_ascii=False,indent=2),encoding='utf-8')
archive=Path(str(package)+'.zip')
assert not archive.exists()
expected={p.relative_to(package).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in package.rglob('*') if p.is_file()}
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
    for name in sorted(expected):output.write(package/name,name)
with tempfile.TemporaryDirectory(prefix='games-zip-check-',dir=root/'work') as directory:
    destination=Path(directory)
    with zipfile.ZipFile(archive) as packed:
        assert packed.testzip() is None
        for name in packed.namelist():
            rel=PurePosixPath(name);assert not rel.is_absolute() and '..' not in rel.parts and ':' not in name
        packed.extractall(destination)
    actual={p.relative_to(destination).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in destination.rglob('*') if p.is_file()}
    assert actual==expected
result=dict(version=version,package=str(package),archive=str(archive),files=len(expected),zip_bytes=archive.stat().st_size,zip_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),independent_extraction_matches=True,production_feed_published=False)
(package.parent/f'CipherChat-Games-{version}-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False),flush=True)
