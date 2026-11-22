# NEXTALK 配套业务与服务端源码

本目录提供 NEXTALK 当前客户端使用的业务层、公共协议和服务端实现。客户端工程位于相邻的 `Messager` 目录。

## 当前使用入口

- 启动：项目根目录的 `启动本机NEXTALK.cmd`。
- 当前客户端版本：0.3.25.0。
- 客户端构建：在 `../Messager` 执行 `build.ps1`，具体命令见 `../Messager/README.md`。
- 完整运行包：`../runtime`；本机更新源：`../local-run/update-feed`。

## 保留的材料

`client`、`common`、`server`、`framework`、`scripts`、`third_party`、`tests` 为当前源码及测试目录。功能边界与证据见 `../local-run/audit-20261114/功能完整性审计.txt`。

旧客户端的多界面启动脚本、历史安装包和构建产物已清理。本次清理仅作用于本地工作区，不改变线上服务及用户账号数据。

## 本机构建与回归

在本目录执行（使用本机已有依赖，不需要 Qt GUI）：

```powershell
$env:PATH = 'D:\msys64\mingw64\bin;' + $env:PATH
cmake -S . -B work/build-local -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/msys64/mingw64 -DBUILD_TESTING=ON
cmake --build work/build-local -j 4
ctest --test-dir work/build-local --output-on-failure
```

根测试中的 TLS 集成用例使用 NEXTALK 当前的无界面 `BusinessWorker`，覆盖口令登录、双向消息、离线补投和文件传输。当前协议为 `ordinary-v1`，传输由 TLS 保护，不再要求身份私钥、预共享密钥或端到端加密；旧协议应被明确拒绝。密码学原语和历史算法的单元测试仍保留。

桌面客户端的完整构建与 13 项 CTest 回归入口为相邻 `Messager/build.ps1`，该入口会先编译全部已注册测试程序。
