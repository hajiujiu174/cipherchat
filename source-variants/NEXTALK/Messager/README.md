# NEXTALK 0.3.25.0

## 本机运行

从项目根目录双击 `启动本机NEXTALK.cmd`。此入口启动 WSL 中的 TLS 服务、Windows 上的 UDP 游戏中转和已安装的 NEXTALK 完整版。

- 当前运行目录：`../runtime/.updates/versions/0.3.25.0`。
- 稳定启动器：`../runtime/nextalk-launcher.exe`，当前版本指针为 `../runtime/current.txt`。
- 聊天和更新服务器：`127.0.0.1:55320`；游戏 UDP 中转：`127.0.0.1:55321`。
- 本机更新源：`../local-run/update-feed/messager`。
- 账号数据与 SMTP 凭据保留在各自独立数据目录，不写入运行包。

## 从当前源码构建

```powershell
.\build.ps1 -QtPrefix D:/msys64/mingw64 -NextalkSource ../nextalk
```

源码构建输出为 `work/build/Messager.exe`。该目录用于开发和测试；普通使用应从完整运行包启动。

从项目根目录生成完整运行包：

```powershell
python local-run/Package-Local.py
```

该入口从当前构建收集 Qt、OpenSSL、SQLite、编译器运行库、更新助手、启动器、版本清单、证书和第三方许可证，同时生成本机更新源。已生成的版本不可覆盖；发布新版本前应同步源码版本号，并指定递增的版本和序号。

完整运行包的回归入口：

```powershell
.\local-run\Test-Local.ps1
```

上条命令在项目根目录运行；它要求运行包和更新源实际存在。当前客户端注册 13 项 CTest，其中真实运行包检查与更新激活/重启/回退均会执行。只有源码、没有完整运行包时，构建目录的单元/UI测试不等于交付验收。

## 功能边界

审计结论、已验证功能和未完成部分见 `../local-run/audit-20261114/功能完整性审计.txt`。
历史 MSI/setup 打包脚本依赖旧运行包材料，当前应使用上述完整运行包生成入口；本目录不宣称已重新交付 MSI/setup 安装包。
