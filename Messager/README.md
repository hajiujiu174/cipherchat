# NEXTALK 0.3.25.0

## 启动与安装

双击工作区根目录的 `启动NEXTALK.cmd`，或使用桌面、开始菜单中的 NEXTALK 快捷方式。

当前运行入口为 `runtime-v0.3.14/NEXTALK.exe`，实际程序版本为 0.3.25.0。目录名用于兼容原有快捷方式，当前版本指针位于 `runtime-v0.3.14/current.txt`。

- 安装版：`artifacts/NEXTALK-0.3.25.0-Setup/setup.exe`
- 便携版：`artifacts/NEXTALK-0.3.25.0-Windows.zip`

## 构建

客户端源码位于本目录，配套业务与服务端源码位于相邻 `cipherchat` 目录。保留现有 Qt、CMake、Ninja、OpenSSL 和 SQLite 依赖。

在本目录执行：

```powershell
.\build.ps1 -QtPrefix D:/msys64/mingw64 -CipherChatSource D:/Work/WORKDONE/cipherchat
```

此入口在 `work/build` 编译客户端并运行检查，客户端输出为 `work/build/Messager.exe`。构建产物与已发布运行包分别管理。

## 目录

- `src`、`resources`、`third_party`：当前源码与依赖资源。
- `tests`：当前测试。
- `work/build`：按需重新生成的构建目录。
- `runtime-v0.3.14`：当前运行目录。
- `artifacts`：最新版安装包和便携包；`server-switch-20261020` 为迁移资料。
- `docs`、`evidence`：技术资料与验证记录。

本地旧安装包、旧运行版本、历史构建产物及其启动脚本已清理。账号数据、迁移资料、当前源码与测试证据保留。
