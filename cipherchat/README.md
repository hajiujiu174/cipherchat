# NEXTALK 配套业务与服务端源码

本目录提供 NEXTALK 当前客户端使用的业务层、公共协议和服务端实现。客户端工程位于相邻的 `Messager` 目录。

## 当前使用入口

- 启动：工作区根目录的 `启动NEXTALK.cmd`，或桌面、开始菜单中的 NEXTALK 快捷方式。
- 当前客户端版本：0.3.25.0。
- 客户端构建：在 `../Messager` 执行 `build.ps1`，具体命令见 `../Messager/README.md`。
- 安装包与便携包：`../Messager/artifacts`。

## 保留的材料

`client`、`common`、`server`、`framework`、`frontends`、`third_party`、`tests` 保留现有源码与资源；`docs` 保留报告与技术资料；迁移记录、测试证据及数据备份保留。

旧客户端的多界面启动脚本、历史安装包和构建产物已清理。本次清理仅作用于本地工作区，不改变线上服务及用户账号数据。
