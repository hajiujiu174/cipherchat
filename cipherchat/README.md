# CipherChat

Messager V0.3.12 已统一浅紫聊天应用图标：登录页、主界面品牌、窗口/任务栏、程序及启动器、桌面和开始菜单入口。已发布在线更新，保留登录前检测与原账号聊天资料。见 [图标验收](docs/brand-icons-20261018.md)。

Messager V0.3.11（2026-10-18）加入登录前自动检查更新：启动后在登录窗口检查，发现新版主动打开更新窗口，无需先登录；用户可下载、退出安装并重新打开新版，账号和聊天资料保留。已上线独立Messager渠道，保留原渠道；旧版需先换新版完整包一次。见 [实现及验收](docs/login-update-20261018.md)。

朋友圈聚合列表已上线（2026-10-18）：Messager V0.3.10 左侧新增朋友圈图标，按时间逆序显示自己及所有好友的文字动态，支持刷新、分页与打开作者资料；他人资料页下方“发消息”直接进入对应聊天。服务器已部署，本机默认入口已更新，旧运行目录保留。见 [实现与验收](docs/moments-feed-20261018.md)。

个人资料与朋友圈已上线（2026-10-18）：默认 Messager V0.3.9 可更换头像、编辑昵称、发布文字动态；从头像、联系人资料或聊天顶部打开个人页，动态按时间倒序，仅自己和好友可见。服务端及公网检查通过，默认入口已切换，旧运行目录保留。见 [个人资料与朋友圈验收](docs/profile-moments-20261018.md)。

忘记密码已上线（2026-10-18）：默认 Messager V0.3.8 加入已验证邮箱验证码重置；本机及目标 Linux 测试、真实 QQ 收信和公网验证通过，原账号身份与数据保留。最终 Linux 构建在本机完成，服务器测试受内存限额保护。见 [重置密码与验证记录](docs/password-reset-20261018.md)。

邮箱注册已上线（2026-10-18）：默认 Messager V0.3.8 注册页提供邮箱、六位验证码和发送倒计时。QQ SMTP 已配置在服务器，真实收信经用户确认；新注册必须通过邮箱验证，原账号直接登录。原 V0.3.6 与服务器回退备份保留。见 [注册邮箱与 SMTP 配置](docs/registration-email-20261018.md)。

当前默认主界面已统一为 **Messager（Qt 6 Widgets）**，替代原 Codex 界面。运行 `启动CipherChat.bat`、桌面或开始菜单的 CipherChat，直接进入 Messager 登录窗口。主界面源码位于相邻的 `../Messager` 工程，沿用本项目原生业务层；后续界面功能在 Messager 维护。

Codex 已从当前启动选项移除，旧源码与历史发行包仅作为归档保留。原双界面、深浅色、自定义主题色根目录脚本均兼容转到 Messager。Rhine 可通过 `启动CipherChat-Rhine.bat` 单独运行；其场景、材质、模型、音效和动画保留。

网页端与本地 HTTP/Node 桥接已退出当前源码、构建和交付入口，原件归档于 `test-tmp/test-host-20261015/legacy-web/`。Qt 场景、材质、模型、音效和动画保留；原来借用网页目录的三段音效已移入原生资源目录，内容哈希不变。

当前 Messager 构建与界面验证使用 `../Messager/build.ps1`；业务层测试入口为 `framework/scripts/test.ps1 -Suite core`。旧界面回归入口 `-Suite ui` 仅供历史兼容检查。Windows 下框架用例及恢复测试子进程经 `framework/build/test-host/cipherchat-tests.exe` 运行，各项 CTest 的名字、断言、超时及独立进程仍保留。详见 [历史变更和验证](docs/test-host-and-native-rhine-20261015.md)。以下发布包和日期条目为历史记录，历史包中的入口不代表当前默认界面。

Windows 安装器：[CipherChat 2026.9.20.1 x64 MSI](artifacts/CipherChat-2026.9.20.1-Windows-x64.msi)。双击安装到当前用户，无需管理员权限；提供桌面与开始菜单快捷方式，保留软件内增量更新，卸载保留账号和聊天记录。[安装、修复与验证说明](docs/installer-20260920.md)。

当前版本 2026.9.20.1：好友须经申请和对方同意；聊天页左侧可切换好友、临时会话、陌生人和好友申请，支持用户名查询及分页浏览。运行项目根 `启动CipherChat.bat`，或下载 `artifacts/CipherChat-Remote-Windows-x64-contacts-20260920.zip`。[联系人使用说明](docs/remote-client-contacts-20260920.md) · [实现与验证](docs/contacts-20260920.md)。

已有带更新模块的客户端可点击左侧“软件更新”，检查中心最新版本、下载缺失数据块并重启更新。更早的旧版首次需手动更换完整包。[更新模块说明](docs/updates-20260919.md)。

历史入口：`启动CipherChat-一键深浅色.bat`。深浅色改为框内文字的胶囊开关，单击即可切换；当前版本也保留该功能。[说明](docs/appearance-switch-20260919.md)。

深浅色版本：运行 `启动CipherChat-深浅色.bat`，右上角选择“浅色 / 深色”，同时支持 GLM / Codex 和自定义主题色，重启记忆。[使用与验证](docs/appearance-20260919.md)。

任意主题色版本：运行 `启动CipherChat-自定义主题色.bat`，点击右上角“主题色”，使用色谱、HEX 或 RGB/HSV 自由选色；GLM 和 Codex 分别记忆颜色。[使用与验证](docs/theme-colors-20260919.md)。

2026-09-19 双界面版本：运行 `启动CipherChat-双界面.bat`，窗口右上角选择 GLM 或 Codex。保留当前会话和草稿，重启恢复设计选择。详见 [双界面说明](docs/dual-design-20260919.md)。

Windows 桌面端到端加密聊天课程设计。已接通账号、身份核对、私聊、文件传输与续传、群聊、在线状态及重启恢复。当前主界面入口为 `../Messager/src`，业务入口为 `ccfw::ClientApplication`；`framework/gui` 内旧界面及旧 `ClientCore` 保留作历史回归。

## 直接运行

当前工作区双击 `启动CipherChat.bat`，或桌面、开始菜单的 CipherChat，直接运行 Messager V0.3.11。也可从相邻 `../Messager` 目录运行 `run.cmd`。启动后先显示登录窗口，已有用户继续使用原账号和原资料目录；两端使用不同账号。构建、运行、功能边界与验证说明见 [Messager 使用说明](../Messager/README.md)。

旧远程包 `artifacts/CipherChat-Remote-Windows-x64-contacts-20260920.zip` 及其中 `start-remote.cmd` 仅为历史归档，并未在此次切换中重新打包。下文原本地演示及发布记录也属于历史说明。

本地演示的历史包为 `artifacts/CipherChat-Windows-x64.zip`，保留 M0—M6 原版实现。在包目录运行：

```powershell
.\start-demo.ps1
```

脚本启动一个服务端和 Alice、Bob 两个窗口。填写各自口令并点“注册并登录”；再次演示用原口令点“登录”。数据默认保存在 `%LOCALAPPDATA%\CipherChat\demo`，关闭两个窗口后脚本退出并停止它启动的服务端。加 `-ThirdClient` 打开第三个窗口；加 `-DataDirectory <新目录>` 建立独立演示。端口被占用时明确报错，可用 `-Port 55221`。

若系统策略阻止 PowerShell 脚本，直接从包目录打开命令行运行 `cipherchat-server.exe 55220`，再双击两次 `cipherchat.exe`。账号页选择服务端生成的 `cert.pem`，地址填 `127.0.0.1`、端口填 `55220`、证书主机名填 `localhost`；两个窗口填写不同用户名和数据目录。不需要改系统执行策略或安装根证书。

首次给陌生联系人发送消息会要求确认身份。双方在账号页复制自己的身份指纹，通过当面等可信渠道比对，再在对端弹窗点击确认。确认后待发消息继续发送。身份变化会再次阻止建立会话。

私聊页可查看签收；文件页选择联系人和文件，查看已验证进度，重启后选中未完成项点“恢复”。接收目录位于该账号 profile 的 `downloads` 下，完成路径显示在文件名的提示中。群聊页填写群名和成员用户名，创建后发送；成员必须已注册，首次群发也需要核对各成员身份。

## Linux 服务器与跨设备通信

Linux IPv6 服务端与预填连接信息的远程 Windows 客户端已验证。使用 `artifacts/CipherChat-Remote-Windows-x64-server2-final.zip`，解压后双击 `start-remote.cmd`；每台 Windows 电脑使用不同账号，即可通过 Linux 服务器通信。两端网络需能访问公网 IPv6，同一账号多设备同步尚未实现。

当前中心服务器为 `[2603:1040:f02:d::21d]:55220`（地址和端口在客户端分别填写）。部署状态、验证证据和管理方法见 `docs/server2-deployment.md`，操作说明见 `docs/remote-client.md`。原部署历史保留在 `docs/linux-deployment.md`。

混合传输版客户端包为 `artifacts/CipherChat-Remote-Windows-x64-hybrid-20260915-r2.zip`；它保留中心 Relay，并在双方可达时优先使用临时 P2P 文件通道。旧的 `server2-final` 包没有被覆盖。

## 构建与验收

本机使用 CMake、Ninja、GNU 16.2、OpenSSL 3.6.4、SQLite 3.53.4 和 Qt 6.11.2。已有依赖路径记录在 presets 中；换机器时修改工具链路径。编译需要 C++20，最低 CMake 3.24、Qt 6.5。不在配置时下载依赖。

```powershell
$env:PATH = 'D:\msys64\mingw64\bin;C:\Qt\6.11.2\mingw_64\bin;' + $env:PATH
cmake --preset core-debug
cmake --build --preset core-debug -j 3
ctest --preset core-debug --output-on-failure

cmake --preset desktop-release
cmake --build --preset desktop-release --target cipherchat-server ccfw-desktop -j 3
.\scripts\package-windows.ps1

cmake -S framework --preset desktop-debug
cmake --build framework/build/framework-desktop -j 3
ctest --test-dir framework/build/framework-desktop --output-on-failure --timeout 90
```

独立纯核心验收在 `framework` 目录执行 `core-debug` preset，共 21 项 CTest；桌面验收增加 `M4-04` 双原生 GUI、`M6-01` 原交付包、`M7-02` 账号作用域和 `M8-01` 直连通道测试，共 25 项。`M6-01` 需要原交付包、方案书和 QA 证据；新版远程包另由 `scripts/test-remote-package.py` 验证。`framework/tasks.json` 为任务与证据索引，原始失败基线保存在 `framework/docs/baseline.md`。

文件升级的 Release 构建、测试和新包复现命令见 `docs/file-transfer-upgrade.md`；该构建输出 `build/file-transfer-release/cipherchat.exe`，远程打包脚本从此路径取新版程序，旧包仅提供相同版本的运行库和许可证。

原项目测试包含 50 个 doctest case。证据见 `artifacts/*-tests.xml`、`artifacts/gui/`、`artifacts/package-smoke.json` 和 `docs/verification.md`。跨进程恢复测试会真实终止并重启测试客户端。

## 实现要点

- TLS 1.3 验证证书链和预期主机名，协商 v2 后才提交注册或登录。自签证书由客户端显式信任，当前服务端命令行默认监听 `::`，接受 IPv6 和 IPv4；旧 Windows 演示包保留原 IPv4 默认。
- 复用唯一 `common/crypto`、X3DH 和 Double Ratchet 实现，底层使用 OpenSSL EVP。未知或改变的身份必须人工确认；认证失败不推进棘轮状态。
- 发送时原子提交棘轮、历史和 outbox，再发送；重试使用原消息 ID 与原密文。接收端先认证、去重和提交，再发送签收。服务端入队确认与对端签收为不同状态。
- 客户端 SQLite 快照由 Windows DPAPI 保护，绑定当前 Windows 用户与账号作用域。网络线程只收发入队，一个业务线程拥有 store/session/services；Qt 通过有界命令和事件队列更新界面。
- 文件采用混合传输：中心服务器负责加密元数据和 Relay，双方先用一次性 token 尝试临时 TCP P2P，失败或超时自动回退 Relay。两条路径共用随机 32 字节文件密钥、64 KiB 分块、16 块窗口、4 块批量持久化、加密累计 ACK、CCF3/CBOR 和 SHA-256；网络线程只把直连 envelope 入队，业务线程处理并持久化。新进程复用已保存的在途密文续传。详见 `docs/hybrid-p2p-relay.md`。
- 群聊为 2–64 人逐成员独立加密，服务端校验双方群成员资格；逐成员记录结果，局部失败只重试失败成员。在线名单来自服务器，输入提示每秒节流、3 秒到期。

## 边界与数据保留

这是自定义课程设计简化协议，不与 Signal 标准协议互通，未经过独立安全审计。服务端能看到账号、路由、群成员、在线状态、大小及时间等元数据；持有 TLS 私钥。注册口令经验证的 TLS 发到服务端处理，聊天正文与文件内容由客户端端到端加密。身份指纹比对仍依赖可信渠道。

DPAPI 保护静态快照，不能防护已控制当前 Windows 用户的进程；账号数据不能直接复制到另一 Windows 用户恢复。缺失或损坏的身份数据库不会自动创建替代身份登录已有账号。当前没有同一账号多设备同步、密码找回、自动身份迁移或群成员编辑。

单文件最大 1 GiB，每端最多 16 个活动传输；每 tick 最多推进 4 个 64 KiB 读取/哈希块，乱序缓存最多 64 块。消息帧上限 8 MiB，网络队列上限 256 帧/16 MiB；服务端并发连接上限 256。达到限制会报错。

发送过程中源文件被只读锁定。恢复时重新校验源文件和接收前缀，发生变化拒绝复用旧文件密钥。取消会保留 `.part` 和取消记录；再次发送创建新的传输，取消项不能恢复。接收的最终文件和源文件属于普通文件，不由 DPAPI 加密。请自行保存仍需保留的文件与原账号数据。

原 Windows 演示包包含程序、动态库和说明，远程客户端包另含服务器公开证书和预填配置启动脚本。发布包均不包含账号数据库、私钥或口令。源码、原材料、历史失败日志与交付证据均保留在项目目录中。

最新混合客户端与启动授权优化（2026-09-16）：`artifacts/CipherChat-Remote-Windows-x64-hybrid-20260916.zip`。文件页可保存“允许文件直连”选择；关闭后走 Relay。启动不监听 P2P 端口，发送时按需监听；实际验证和边界见 `docs/hybrid-verification-20260916.md`。

2026-09-16 聊天页合并版：文件发送、进度、恢复/取消与直连开关均在“聊天”页，文件默认发给当前联系人。新包 `artifacts/CipherChat-Remote-Windows-x64-chat-files-20260916.zip`；根目录 `启动CipherChat.bat` 指向新包。详见 `docs/chat-files-20260916.md`。

2026-09-16 界面精简版：主界面移除常驻指纹及聊天核对按钮，服务器默认收进高级设置；证书配置已从界面及高级设置移除，由启动配置自动提供并验证。必要核对信息可按需打开。验证见 `docs/certificate-ui-20260916.md`，前一版记录保留在 `docs/clean-ui-20260916.md`。

此前文件折叠框版的记录保留在 `docs/collapsible-files-20260916.md`。

最新聊天文件卡片版（M8-04）：文件按实际发送时间穿插在文字消息之间，卡片内显示进度与操作，重新登录后位置保持。底部“发送文件”仅展开附件工具；独立文件表格已移除。旧记录没有时间则标注历史文件。当前 `启动CipherChat.bat` 指向 `artifacts/CipherChat-Remote-Windows-x64-timeline-files-20260916`；完整26/26回归及交付见 `docs/timeline-files-20260916.md`。
