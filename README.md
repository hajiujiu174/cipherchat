# NEXTALK 0.3.26.1 重构版

桌面客户端和服务端各自编译、各自启动，通过 TLS 交换 `shared` 定义的消息。代码沿用原版消息格式和业务实现。密码登录由客户端在完成证书验证的 TLS 连接上提交，服务端使用已有的密码哈希校验。新版密码登录需要同时更新客户端和服务端，账号数据库可直接沿用。

| 目录 | 职责 |
|---|---|
| `frontend/src/ui` | 主窗口、基础组件、通用界面 |
| `frontend/src/features` | 登录、聊天、群聊、文件、好友与动态、通话、便签待办、小游戏的页面与适配 |
| `frontend/runtime` | 客户端业务线程、消息与文件服务、本地数据库和网络连接 |
| `backend/server` | 服务启动、连接接入、路由、账号认证、消息投递、文件、群聊、好友、通话和游戏转发 |
| `shared` | 消息格式、协议帧、TLS 基础能力和双端使用的类型 |
| `frontend/tests`、`backend/tests` | 桌面和服务端的测试，联机测试复用服务端源码 |

## Windows 上运行

在当前演示机上，以下入口使用已安装的 Windows 完整版和同一个 WSL 2 服务端。`Client` 打开一个客户端，`Demo` 打开两个演示客户端，`Server` 只启动服务端和游戏中转。

```powershell
.\scripts\Run-Local.ps1 -Mode Client
.\scripts\Run-Local.ps1 -Mode Demo
.\scripts\Run-Local.ps1 -Mode Server
```

聊天、注册和更新通过 WSL 的 `127.0.0.1:55320`，游戏 UDP 中转通过 Windows 的 `127.0.0.1:55321`。演示数据库保存在 WSL，演示客户端数据保存在本项目的 `.local/wsl-demo`。

编译和运行测试仍使用现有 MSYS2 MinGW64、Qt 6、CMake、Ninja、OpenSSL、SQLite 和 libcurl。

```powershell
.\scripts\Build.ps1 -Target All -Test
```

生产编译只依赖本端和 `shared`，开启测试时引入对端源码做联机验证。平时从以上入口构建和运行，随源码保留的旧发布工具需配合发布目录和材料使用。

## 单独构建文本消息收发核心

正式客户端调用 `frontend/runtime/chat_core` 中的 C++98 流程。该模块负责基本文本条件、发件箱保存与网络提交的顺序，以及落盘后签收和重复包补签收。TLS、JSON、存储事务和界面仍由项目的现有接口提供。

```powershell
cmake -S frontend/runtime/chat_core -B build/chat-core-cpp98 -G Ninja
cmake --build build/chat-core-cpp98
ctest --test-dir build/chat-core-cpp98 --output-on-failure
```

## 读源码与答辩

从主窗口提交一条消息，顺着 `frontend/src/features/chat/nextalkchat.cpp`、客户端 `ChatService`、服务端 `backend/server/chat/delivery.cpp` 看它如何通过本地发件箱和服务端投递队列到达对方。目录与入口见 [模块导航](<D:/Work/WORKDONE/NEXTALK-Refactor/docs/模块导航.md>)。

原版的代码量记录保存在 `docs/原版源码统计`。清单使用原版路径，小游戏及其联机适配单列。交作业时按实际贡献核实独立撰写的行数。第三方源码与许可证保留在相应的 `third_party` 目录。
