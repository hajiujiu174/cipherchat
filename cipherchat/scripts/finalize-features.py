"""Register M10 completion only after local runtime, package and document evidence exists."""
from pathlib import Path
import hashlib, json, xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent.parent
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def passed_xml(path, count):
    cases=list(ET.parse(path).getroot().iter('testcase'))
    assert len(cases)==count and all(c.find('failure') is None and c.find('error') is None for c in cases),path
    return {c.attrib['name']:float(c.attrib.get('time',0)) for c in cases}

def main():
    if not (ROOT/'framework/demo/runner.cpp').exists():
        raise RuntimeError('这是2026-10-08演示版本的历史结项脚本；演示已移除，当前证据见 docs/remove-demo-20261011.md。')
    core=passed_xml(ROOT/'framework/build/features-core/core-final.xml',29)
    gui_cases=list(ET.parse(ROOT/'framework/build/features-release/gui-final-15.xml').getroot().iter('testcase'))
    assert len(gui_cases)==15
    gui={c.attrib['name']:float(c.attrib.get('time',0)) for c in gui_cases if c.find('failure') is None and c.find('error') is None}
    assert set(c.attrib['name']for c in gui_cases)-set(gui)=={'UI-hot-switch'}
    gui.update(passed_xml(ROOT/'framework/build/features-release/opening-16.xml',1))
    runtime=passed_xml(ROOT/'framework/build/features-core/runtime-final-16.xml',1)
    metadata=passed_xml(ROOT/'framework/build/features-core/features-metadata-19.xml',3)
    packages=json.loads((ROOT/'artifacts/features-package-result-20261008-r2.json').read_bytes())
    package_test=json.loads((ROOT/'test-tmp/features-package-20261008-r4/result.json').read_bytes())
    assert package_test['passed'] and package_test['start_scripts_tested'] and not package_test['public_server_modified']
    assert package_test['version']==packages['version']=='2026.10.8.2'
    for package in packages['packages']: assert sha(Path(package['zip']))==package['sha256']
    dependency=json.loads((ROOT/'test-tmp/features-20261007/dependency-final-20.json').read_bytes())
    assert dependency['passed'] and not dependency['Qt_commands']
    assert not any('qt' in dll.lower() for dll in dependency['runtime_imports'])
    qa=json.loads((ROOT/'test-tmp/features-document-20261008/final-qa.json').read_bytes())
    assert qa['visual_review']=='passed' and 25<=qa['pages']<=35 and not qa['blank_tail']
    assert sha(Path(qa['document']))==qa['document_sha256']
    original=ROOT/'docs/答辩/CipherChat项目技术文档_答辩版_20261007.docx'
    assert sha(original)=='5025a301e3f665522b95388a9c19a48455ada9cfd2a1475b2b4ce8da44231414'
    evidence={
      'date':'2026-10-08','configuration':'core Debug without Qt / desktop Release x64',
      'core':'test-tmp/features-20261007/core-tests-final-01.log',
      'runtime':'test-tmp/features-20261007/runtime-tests-final-16.log',
      'gui':'test-tmp/features-20261007/gui-tests-final-15.log; opening-tests-16.log',
      'package':'test-tmp/features-package-20261008-r4/result.json',
      'metadata':'test-tmp/features-20261007/core-metadata-tests-19.log',
      'document':'test-tmp/features-document-20261008/final-qa.json','publicServer':'unchanged; local delivery only'}
    items=[
      ('M10-00','基线与公共接口设计','独立核心与桌面构建；冻结原源码、旧端和界面基线。','原基线核心25/25、桌面35/35；原DOCX和原包保留；接口变更先登记。','baseline-core-tests.log、baseline-desktop-tests.log、source-baseline.zip'),
      ('M10-01','安全过程可视化','真实提交、队列、服务器接受与对端签收位置发布有界阶段事件。','提交失败不播放成功；重试/重复ACK；切换后安全状态一致；不表示已读。','M10-features、UI-hot-switch'),
      ('M10-02','本地演示环境','本机回环TLS服务、独立随机身份、两个客户端和拥有句柄的启动器。','无公网真实收发；新运行目录；不按程序名终止进程；失败和退出处理。','M10-demo-process、M10-demo-controls、独立ZIP offline-run/report.json'),
      ('M10-03','故障演练台','演示专属适配器注入断线、首次签收丢失、重复、合法结构密文篡改；检查点后结束受控接收进程并恢复文件。','消息ID/重试密文相同；接收只一次；认证失败不推进历史/棘轮；恢复文件逐字节与SHA核对；超时不假通过。','演示六个场景及JSON/可读报告；M10-demo-ui'),
      ('M10-04','答辩演示模式','三步导航、隐私显示、原正式窗口保留、测试GUI客户端、首次完整Qt开场。','正式会话和传输继续；退出恢复界面与草稿；真实新消息签收；拥有的子进程退出。','M10-demo-ui、UI-hot-switch、独立演示ZIP宿主启动'),
      ('M10-05','可调整工作台','实际逻辑尺寸、宽屏分隔、窄屏标签、联系人抽屉、专注、四个固定会话、草稿和滚动位置。','960×640/1366×768/1920×1080，深浅与1/1.25/1.5缩放；底部输入可达；过期保存回执不覆盖新编辑。','M10-workspace三项、M4-04-layout、UI-appearance；最终PNG'),
      ('M10-06','视觉身份卡','完整SHA-256、辅助图案、粘贴、带作用域文本导入导出与二维码；固定MIT来源。','错误/损坏/作用域/旧公钥拒绝；PNG独立解码匹配真实公钥及指纹；同指纹不重开闪烁，密钥变化禁用旧确认。','M10-features、M10-workspace内身份事件回归、test-identity-qr.py、GUI身份确认次数'),
      ('M10-07','多文件交付卡与版本关联','私聊1～16文件，清单不可变、确定任务ID、单项恢复、取消、加权进度、人工确认与版本关联。','零字节/同名/多文件/失败/离线/重启；提前和错误摘要确认拒绝；重复幂等；真实旧端收说明与文件，旧端写库保留扩展。','M10-features六组场景；冻结旧进程真实TLS兼容及DPAPI/CAS测试'),
      ('M10-08','综合验收与本地交付','正式ZIP、独立演示ZIP、本地feed、独立解压验收、任务/契约和另存答辩稿。','包内依赖启动；本机真实TLS下载与旧版→新版→旧版回退、资料标记保留；Word PDF所有页检查；不上传公共服务器。','独立包result.json、manifest.json、包SHA256、文档final-qa.json'),
      ('M10-09','更新下载恢复修复','固定最多四路块下载、独立TLS连接、有限重连、缓存、暂停/继续及无响应看门狗。','真实TLS无回复与断线重连；哈希/证书不绕过；缓存继续；未完整校验不能激活；速度不承诺所有网络固定倍数。','M10-updates三组、UI-updates、独立包本地stage/activate/rollback日志')]
    path=ROOT/'framework/tasks.json';manifest=json.loads(path.read_bytes());by_id={t['id']:t for t in manifest['tasks']}
    if 'M10-09' not in by_id['M10-08']['deps']: by_id['M10-08']['deps'].append('M10-09')
    allowed=['framework/include/ccfw','framework/src','framework/gui','framework/demo','framework/tests','framework/update','framework/CMakeLists.txt','framework/CMakePresets.json','framework/tasks.json','framework/tasks','framework/docs','docs','scripts','third_party/qrcodegen']
    for task_id,title,implementation,accept,proof in items:
        task=by_id.get(task_id)
        if task is None:
            task={'id':task_id,'title':title,'deps':['M10-00'],'milestone':'M10','card':'tasks/'+task_id+'.md'};manifest['tasks'].append(task)
        task.update(status='done',files=allowed,allowedFiles=allowed,steps=[implementation],accept=[accept],evidence={**evidence,'scenarios':proof})
        card=f'# {task_id} {title}\n\n状态：完成，仅覆盖下列验收。\n\n## 前置\n\n'+(', '.join(task['deps']) or '用户确认的完整实施计划')+'。\n\n## 允许修改范围\n\n'+ '\n'.join('- '+s for s in allowed)+'\n\n## 实现\n\n'+implementation+'\n\n## 验收场景\n\n'+accept+'\n\n## 证据\n\n'+proof+'\n\n统一证据索引：`../../docs/features-20261008.md`；失败批次保留于 `../../test-tmp/features-20261007`，成功结果追加，不覆盖失败。\n\n## 边界\n\n共用唯一账号业务线程与原密码/文件算法；演练注入仅链接演示组件；本批包只本地保存，公共服务器未改。更新器固定下载线程属于独立进程，不拥有账号或会话。\n'
        (ROOT/'framework'/task['card']).write_text(card,encoding='utf8')
    manifest['completion'].update(date='2026-10-08',done=sum(t['status']=='done'for t in manifest['tasks']),total=len(manifest['tasks']),verification='../docs/features-20261008.md')
    path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    rows='\n'.join(f'| {n} | 通过 | {t:.2f} s |' for n,t in gui.items())
    package_rows='\n'.join(f"| {Path(p['zip']).name} | {p['bytes']} | `{p['sha256']}` |"for p in packages['packages'])
    doc=f'''# CipherChat M10 本地交付记录（2026-10-08）

版本 **2026.10.8.2**。用户明确要求新包只保存在本地。本批没有上传、替换或重启公共服务器；公共服务仍为排查时读取的旧版本，不能通过在线更新获得本批新功能。

## 已实现功能

真实消息阶段详情、独立本机TLS双客户端、四类消息故障及文件进程恢复、三步答辩演示、两套界面共享草稿/选择/固定会话、宽屏并排与窄屏标签、完整指纹身份卡和二维码、私聊多文件交付/人工确认/版本关联。首次Qt开场和三维磨砂背景保留；热切换继续使用当前会话和场景。

交付数据使用同库DPAPI扩展表，与旧快照在同一CAS事务提交。加密chat可选扩展携带清单及人工确认，旧端仍接收普通说明和文件。人工收到与协议签收分开。二维码来源、固定提交、摘要和MIT许可在 `third_party/qrcodegen/SOURCE.md` 与包内licenses。

## 下载慢与卡住

旧更新器每次串行请求256 KiB块，往返和握手开销明显；下载阶段缺少自动重连/重试，重开会读取已验证缓存继续。本机公网探测发生过块接收超时，服务日志有TLS更新响应失败；现场另一台电脑的版本和日志缺失，无法确认其每次停止的链路原因，也不能归咎校园网整体断网。

新更新器最多四个固定下载线程，每线程独立TLS及请求ID，网络超时/断开后最多四次尝试，间隔回退；证书、协议和摘要错误直接拒绝。界面可暂停/继续，45秒无输出触发受控重启；所有块和完整文件验证完成后才开放激活。初始连接失败仍明确报错供用户重试；不承诺四倍速度。旧更新器必须先完成一次更新或改用本地完整ZIP才能获得修复。

## 回归中修复的问题

- 登录恢复快照晚于Online，覆盖刚编辑的会话。现在先恢复快照再开放聊天。
- 过期工作台保存回执回滚新草稿。主窗口协调嵌入桥的最新状态，过期回执丢弃。
- 确认后的已核对身份回执又打开核对卡，导致闪烁。现在更新状态，同一指纹去重；旧密钥确认在密钥变化时禁用。GUI确认次数和重复事件保留输入测试覆盖该问题。
- 窗口退出时身份卡销毁回调引用已释放Impl，已解除关闭时的子对象回调。
- 演示长路径超过Windows传统限制，改用短的每轮专用根；目录和上一轮证据保留。
- 浅色工作区受Windows深色原生页签背景影响，新增明确容器背景，并读取渲染像素验证。

失败日志原样保留，主要批次为 `gui-tests-final-10.log`、`m4-diagnostic-tests-11.log`、`workspace-order-tests-13.log` 和 `gui-tests-final-14.log`。14批工作台失败来自新增加的测试先打开账号卡后未返回聊天，15批补足导航操作，保留全部可见/边界断言。

## 验证与复现

原基线核心25/25、桌面35/35。无Qt Debug核心29/29通过；业务worker最终调整后另复核M7-02（{sum(runtime.values()):.2f}s）。桌面Release第15批14/15通过，热切换动画开始等待曾超时；第16批补足首次窗口曝光前置，完整开场、登录与文件热切换通过，后续时间和边界断言保留。15项桌面最新有效结果均通过，来源为两个批次，未拼造全套成功JUnit。不同批次与标签不能直接累加为一个同时执行的测试数。

| 桌面验收 | 结果 | 耗时 |
|---|---|---|
{rows}

最终演示版本元数据更新为2026.10.8.2后，受影响的M10-features、M10-demo-process和M10-demo-controls另行复核3/3通过（{sum(metadata.values()):.2f}s），结果为 `features-metadata-19.xml`，不与前述全套结果重复累加。

主要日志在 `test-tmp/features-20261007`；JUnit在对应独立build目录。核心构建用GCC16.2，GUI用Qt6.11.2配套GCC13.1及包内相同运行库。最终核心编译命令、runtime导入检查与源码摘要见 `dependency-final-20.json` 和 `source-final-manifest-20.json`；第16批记录保留。

工作台三种逻辑尺寸×两主题×三缩放有最终截图；QR从PNG独立读取并核对完整公钥指纹。心跳阈值为max(250ms,2×同机旧场景基线)，包含首次三维/音频资源加载，基线本身超过1秒，不能称为250ms稳定延迟。具体结果在workspace JUnit输出中。

独立包检查不使用Qt开发目录PATH。先核对包内manifest全部字节/摘要，通过包内启动脚本真实启动两套UI及演示宿主，运行六个本机演练，再使用专用本机TLS更新服务，从真实旧GUI激活新版、重启、回退旧GUI并核对文件/资料标记。结果：`test-tmp/features-package-20261008-r4/result.json`；stage/activate/rollback日志同目录。第一次关闭宿主的驱动选中了不带标题的辅助窗口，改为匹配答辩导航与恢复后的宿主标题，原失败目录保留。首次2026.10.8.1包缺少README提及的start-remote.cmd，且残留旧README.txt；新-r2包补齐实际入口、统一说明并更新版本为2026.10.8.2，旧包和其通过记录保留。第19批冷启动探测的通用窗口等待超时，驱动改为按实际标题查找可见窗口，在20秒总期限内验证窗口响应；第20批实际入口验收通过，原失败日志及目录保留。未操作用户正式账号库或系统安装记录。

复现入口：`scripts/test-features-package.py`（两个ZIP及新的工作目录）、`scripts/test-identity-qr.py`（workspace截图目录）、`framework/scripts/verify.ps1 -AuditOnly`。演示启动器只管理拥有的进程句柄；每轮文件有专用标记和独立路径。

## 包与使用方法

| 包 | ZIP字节 | SHA-256 |
|---|---|---|
{package_rows}

正式ZIP解压到新的独立目录，用 `start-remote.cmd` 或稳定启动器；旧包保留。演示ZIP用 `start-demo.cmd`，无需正式账号和公网。首次Qt开场约35秒，纳入答辩时间；三步依次查看加密与签收、故障与恢复、保持登录的往返热切换，再发送新消息并核验签收。关闭导航恢复宿主。

答辩稿另存 `docs/答辩/CipherChat项目技术文档_M10更新稿_20261008.docx`，{qa['pages']}页；Word更新目录并导出PDF，Poppler所有页检查。原稿SHA256仍为5025a301e3f665522b95388a9c19a48455ada9cfd2a1475b2b4ce8da44231414。PDF预览与视觉证据在文档专用临时目录。

## 实际边界

私聊交付首版，不含群交付/内容差异/摄像头扫码/多设备恢复。没有独立协议安全审计、离线发布签名、实体异机最终验收、长时间压力测试；故障演练为本机真实TLS和真实进程，不代表所有公网情形。另一台电脑仍需使用新包后确认现场下载体验。本批新包没有服务器发布步骤。
'''
    (ROOT/'docs/features-20261008.md').write_text(doc,encoding='utf8')
    print(json.dumps({'registered':10,'local_only':True,'GUI_tests':len(gui),'document_pages':qa['pages']},ensure_ascii=False))
if __name__=='__main__': main()
