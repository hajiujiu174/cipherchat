"""Create an immutable local client package without defence demonstration tools."""
import argparse, hashlib, importlib.machinery, json, shutil, zipfile
from pathlib import Path

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build",type=Path);parser.add_argument("base",type=Path);parser.add_argument("output",type=Path);parser.add_argument("feed",type=Path)
    parser.add_argument('--version',default='2026.10.12.1');parser.add_argument('--sequence',type=int,default=2026101201)
    parser.add_argument('--guide',type=Path);parser.add_argument('--notes',default='Qt右侧工作区、会话搜索与快捷操作、历史和草稿恢复；保留全部动画、登录热切换与更新恢复')
    parser.add_argument('--default-ui',choices=['codex','rhine'],default='rhine')
    parser.add_argument('--default-skin',choices=['classic','pencil'])
    parser.add_argument('--public-update',action='store_true')
    parser.add_argument('--compressed',action='store_true')
    parser.add_argument('--prior-release',type=Path)
    args=parser.parse_args();build=args.build.resolve();base=args.base.resolve();formal=args.output.resolve()
    for target in [formal,formal.with_suffix('.zip')]:
        if target.exists():raise RuntimeError('Choose a new output; previous packages are preserved: '+str(target))
    formal.mkdir(parents=True)
    for path in base.rglob('*'):
        if path.is_symlink():raise RuntimeError('Links are not allowed')
        relative=path.relative_to(base)
        if relative.parts[0]=='.updates' or relative.as_posix() in ['manifest.json','release.json','current.txt','previous.txt']:continue
        if path.name.startswith(('cipherchat-demo-', 'start-demo')):continue
        if relative.parts[:2] == ('frontends','rhine') or relative.parts[0] in {'node_modules','rhine-web'} or path.name in {'cipherchat-rhine-core.exe','start-web.cmd','server.mjs','node.exe'}:continue
        if path.is_file():target=formal/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
    for name in ['cipherchat.exe','cipherchat-rhine.exe','cipherchat-updater.exe','cipherchat-launcher.exe']:
        shutil.copy2(build/name,formal/name)
    toolchain=Path(r'C:\Qt\Tools\mingw1310_64\bin')
    for name in ['libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll']:shutil.copy2(toolchain/name,formal/name)
    project=Path(__file__).resolve().parent.parent
    notice=(formal/'THIRD_PARTY.md').read_text(encoding='utf-8-sig')
    (formal/'THIRD_PARTY.md').write_text(notice+'\n\nNayuki QR Code generator v1.8, MIT. Fixed source and license: licenses/qrcodegen/SOURCE.md and qrcodegen.hpp.\n',encoding='utf8')
    qr=formal/'licenses/qrcodegen';qr.mkdir(parents=True,exist_ok=True)
    for name in ['SOURCE.md','qrcodegen.hpp']:shutil.copy2(project/'third_party/qrcodegen'/name,qr/name)
    selected_ui = (' --ui '+args.default_ui) if args.default_ui else ''
    (formal/'start-remote.cmd').write_bytes(('@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0cipherchat-launcher.exe"'+selected_ui+' %*\r\n').encode('ascii'))
    (formal/'start-codex.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0cipherchat-launcher.exe" --ui codex %*\r\n')
    (formal/'start-qt.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0cipherchat-launcher.exe" --ui rhine %*\r\n')
    guide=args.guide or project/'docs/qt-usability-20261012.md'
    if guide.exists():shutil.copy2(guide,formal/'USAGE.md')
    readme='''# CipherChat VERSION_PLACEHOLDER

运行 start-remote.cmd 或 cipherchat-launcher.exe。账号和资料仍使用原数据目录。

本批仅本地交付，未上传公共服务器。请解压到新的独立目录运行，原安装包保留。

双击 start-qt.cmd 进入Qt复刻版，或运行 start-remote.cmd --ui rhine。两套界面共用登录、草稿和当前会话，Qt 保留完整开场及全部原动效。左侧模型完整显示，右侧未登录时为紧凑登录区，登录后展开操作区和五模块导航；会话可搜索和单击切换，窄窗口使用抽屉。Enter发送、Shift+Enter换行，设置中可恢复Ctrl+Enter方式。消息区独立滚动，历史分批加载，草稿和阅读位置随会话保留。附件自动带入选择时的接收人；传输按需展开。详见中文操作说明 USAGE.md。

软件更新增加自动重连、最多四路分块、暂停及继续。已校验内容保留，全部校验成功后才能退出并更新；可回退上一版。整体网络可用不保证每条连接持续返回，速度仍受实际链路影响。

本版已移除答辩演示入口、故障演练、专用本机服务和测试客户端。普通聊天、文件传输、身份卡、工作台及热切换正常保留。真实账号库、私钥和聊天正文不包含在安装包中。
'''.replace('VERSION_PLACEHOLDER',args.version)
    if args.default_ui == 'codex':
        readme = f'''# CipherChat {args.version}

运行 start-codex.cmd 或 start-remote.cmd，进入 Codex 桌面版。桌面快捷方式使用 --ui codex。直接运行启动器时沿用已有界面偏好。

本次更新：{args.notes}。详见 USAGE.md。

登录界面提供“记住账号”和“记住密码”。默认不勾选；记住密码会同时记住账号，取消记住账号会清除两项保存内容。仅成功登录后保存，下次打开填入但不会自动登录。密码用 Windows 当前用户保护保存，错误登录不会覆盖已保存的正确密码。

保留原账号和资料目录。请解压到新的独立目录运行，旧包保留。本批仅本地交付，未替换安装目录、未发布服务器。Qt Rhine 沿用原版，可用 start-qt.cmd 打开。

包内不包含真实账号、私钥和聊天记录。源码和本轮验收记录另存。
'''
        if args.public_update:
            readme=readme.replace('本批仅本地交付，未替换安装目录、未发布服务器。','此版本用于本地运行和在线更新，程序更新使用独立版本目录。')
    if args.default_skin:
        (formal/'appearance-defaults.json').write_text(json.dumps({'skin':args.default_skin},indent=2),encoding='utf8')
    (formal/'README.md').write_text(readme,encoding='utf8')
    (formal/'README.txt').write_text(readme,encoding='utf8')
    feed=importlib.machinery.SourceFileLoader('feature_feed',str(project/'scripts/build-update-feed.py')).load_module()
    feed.build(formal,args.feed.resolve(),args.version,args.sequence,args.notes,args.compressed,args.prior_release)
    manifest=json.loads((formal/'release.json').read_bytes());assert len(manifest['files'])<=2048 and sum(f['size']for f in manifest['files'])<=512*1048576
    report={'version':args.version,'sequence':args.sequence,'formal_files':len(manifest['files']),'formal_payload_bytes':sum(f['size']for f in manifest['files']),'packages':[]}
    for target in [formal]:
        files=[{'path':p.relative_to(target).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)}for p in sorted(target.rglob('*'))if p.is_file()]
        (target/'manifest.json').write_text(json.dumps({'configuration':'Release','architecture':'x64','files':files},indent=2),encoding='utf8')
        archive=target.with_suffix('.zip')
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
            for p in sorted(target.rglob('*')):
                if p.is_file():z.write(p,target.name+'/'+p.relative_to(target).as_posix())
        report['packages'].append({'directory':str(target),'zip':str(archive),'bytes':archive.stat().st_size,'sha256':sha(archive)})
    report_path=formal.parent/('features-package-result-'+formal.name.removeprefix('CipherChat-Features-')+'.json')
    if report_path.exists():raise RuntimeError('Previous package report is preserved: '+str(report_path))
    report_path.write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report))

if __name__=='__main__':main()
