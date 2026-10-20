# CipherChat 桌面启动器

Messager 已替代 Codex，作为默认和后续维护的主界面。双击桌面、开始菜单的 CipherChat，或运行项目根目录 `启动CipherChat.bat`，直接打开 Messager V0.3.11 的登录窗口。登录成功后才显示聊天主界面。

运行启动器 `--choose` 可按需显示两个选项：

- **Messager · 默认界面**：启动 V0.3.11，先显示登录 / 注册窗口。
- **Rhine · 动态界面**：通过原版启动器传入 `--ui rhine`。

Codex 已退出可选列表；旧的 `--ui codex` 参数兼容转到 Messager。旧源码和历史发布包作为归档保留，账号资料不迁移、不清理。原来的 `CipherChat 动态 UI` 桌面快捷方式及 `启动CipherChat-Rhine.bat` 保持原样。根目录原双界面、深浅色、自定义主题色等脚本统一转到 Messager。

```powershell
# 默认直接打开 Messager
.\CipherChat-UI-Selector.exe
.\CipherChat-UI-Selector.exe --ui messager

# 历史参数兼容转到 Messager
.\CipherChat-UI-Selector.exe --ui codex

# 仅在需要时显示 Messager、Rhine 选择窗口
.\CipherChat-UI-Selector.exe --choose

# 客户端参数放在分隔符后
.\CipherChat-UI-Selector.exe --ui messager -- --demo
```

## 本机部署

- 源码：`D:\Work\WORKDONE\Messager\tools\desktop-launcher`
- 程序：`D:\Work\WORKDONE\Messager\runtime-v0.3.1\CipherChat-UI-Selector.exe`
- 路由配置：同目录 `desktop-frontends.json`
- 桌面入口：`C:\Users\xyd06\OneDrive\Desktop\CipherChat.lnk`
- 当前主程序：`D:\Work\WORKDONE\Messager\runtime-v0.3.11\Messager.exe`
- 开始菜单入口：`C:\Users\xyd06\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\CipherChat\CipherChat.lnk`
- 脚本入口：`D:\Work\WORKDONE\cipherchat\scripts\start-messager.cmd`
- 修改前备份：`D:\Work\WORKDONE\Messager\work\backup-default-messager-20261018-011012`

启动器继续使用 `runtime-v0.3.1` 中的 Qt DLL，实际客户端版本由同目录路由配置指定。更新 Messager 时修改这一份配置即可；保留稳定启动器所在目录及运行库。以后迁移整个工作区时需同步修改快捷方式和配置中的绝对路径。

## 构建和检查

在此目录执行 `./build.ps1`，默认使用本机已有的 `D:/msys64/mingw64` Qt 与编译器。输出在 `Messager/work/desktop-launcher-build`，复制生成的 EXE 到现有运行目录后使用。

`CipherChat-UI-Selector.exe --check <输出.json>` 只检查配置中的文件与工作目录，不启动客户端、不访问网络。当前检查结果保存在 `Messager/evidence/messager-default-launcher-check.json`。

## 默认入口切换验证（V0.3.5）

- 启动器已重新构建；可选窗口通过 Windows UI 检查，仅显示 Messager 和 Rhine。
- 桌面、开始菜单、无参数默认启动、历史 `--ui codex` 均打开 `runtime-v0.3.5/Messager.exe` 的 `CipherChat · 登录` 窗口，没有新启动旧界面。
- 原双界面脚本的参数转发通过隔离演示截图检查，未连接网络。
- 快捷方式修改后已重新读取核对，原图标保留，原件及哈希清单已备份。
- 本次未提交登录、注册或发送消息，未重新生成历史 MSI 与归档发布包。详见 `Messager/evidence/MESSAGER_DEFAULT_UI.md`。

如需恢复，按备份中的 `manifest.json` 恢复对应原件；恢复操作会重新启用旧入口。

V0.3.6 更新群聊成员下拉多选，默认路由同步到 `runtime-v0.3.6`。本次启动及群聊验证见 `evidence/GROUP_PICKER_V0.3.6.md`。

V0.3.7（2026-10-18）新增注册邮箱与验证码，默认路由切换到 `runtime-v0.3.7`；原 V0.3.6 目录和切换前配置保留。QQ 实发、用户收件、Linux 服务升级及物理 IPv6 公网探测通过，见 `../cipherchat/docs/registration-email-20261018.md`。

V0.3.11（2026-10-18）上线忘记密码与已验证邮箱重置；默认路径切换到 `runtime-v0.3.11`。测试及低内存服务器部署见相邻 CipherChat 的 `docs/password-reset-20261018.md`。

V0.3.11的默认程序为runtime-v0.3.11/cipherchat-launcher.exe，按current.txt进入已安装的更新版本；不再固定运行初始Messager.exe。
