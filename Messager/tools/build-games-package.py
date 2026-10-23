"""Build a portable games preview from the current ordinary-chat runtime."""
from pathlib import Path, PurePosixPath
import argparse,datetime,hashlib,json,shutil,tempfile,zipfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',type=Path,default=root/'runtime-v0.3.14/.updates/versions/0.3.15.0')
args=parser.parse_args()
version='0.3.17.0'
package=root/f'artifacts/CipherChat-Games-{version}-Windows'
package.mkdir(exist_ok=False)
payload=package/'.updates/versions'/version
shutil.copytree(args.baseline,payload)
shutil.copy2(root/'work/build/Messager.exe',payload/'Messager.exe')
shutil.copy2(root/'runtime-v0.3.14/cipherchat-launcher.exe',package/'cipherchat-launcher.exe')
(package/'current.txt').write_text('.updates/versions/'+version,encoding='ascii')
(package/'启动CipherChat.bat').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nstart "" cipherchat-launcher.exe\r\n')
for game in ['connectfour','reversi','tanktrouble']:
    target=payload/'licenses'/game;target.mkdir(exist_ok=False)
    for name in ['LICENSE','SOURCE.json','README.md']:shutil.copy2(root/'third_party'/game/name,target/name)
with (payload/'licenses/THIRD_PARTY_NOTICES.txt').open('a',encoding='utf-8') as stream:
    stream.write('\nFour in a row: Dev Share Academy, MIT; see connectfour/.\nReversi: Kaahan Motwani, MIT; see reversi/.\nTankTrouble: Copyright (c) 2023 赵睿, MIT; see tanktrouble/.\n')
shutil.copy2(root/'docs/ONLINE-GAMES.md',payload/'ONLINE-GAMES.md')
(package/'使用说明.txt').write_text('CipherChat '+version+' 坦克动荡联机预览版\n\n完整解压后运行“启动CipherChat.bat”。小程序新增坦克动荡：迷宫、反弹炮弹、双人在线房间及聊天邀请。\nWASD 或方向键移动，空格开火，点击战场获得键盘焦点。每方 5 发在场炮弹，自己的反弹弹也会击中自己；先得 5 分获胜。\n坦克联机需要服务器同步升级启用 game_relay；旧服务器会明确提示尚未启用，普通聊天和棋类仍可使用。此包未发布在线更新、未切换桌面安装或生产服务器。\n临时网络延迟会暂停等待；真正掉线后需重新邀请开局。离开页面会释放按键。好友选择和邀请列表均在游戏页；也可从聊天邀请卡片加入。\n原有五子棋、四子棋、黑白棋、便签、待办、科学计算器和邮箱登录保留。账号及聊天历史不包含在包中。\n开源项目和 MIT 许可见 licenses/tanktrouble、connectfour 和 reversi。\n',encoding='utf-8')
release=json.loads((payload/'release.json').read_bytes())
release.update(version=version,sequence=max(release['sequence']+1,int(datetime.datetime.now().strftime('%Y%m%d%H%M'))),notes='新增坦克动荡双人实时对战：迷宫、反弹炮弹与好友邀请。坦克需服务器同时启用 game_relay。')
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
