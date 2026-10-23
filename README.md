# NEXTALK

NEXTALK 0.3.25.0 即时聊天桌面应用，保留紫色图标。本仓库包含 `Messager/` 桌面客户端与 `cipherchat/` 配套业务和服务端源码。

支持用户名或邮箱密码登录、邮箱验证码登录、文字聊天、文件传输、小程序、科学计算器与九款小游戏。网络连接保留 TLS；不使用端到端加密和用户身份私钥。

Windows 构建：在 Messager 目录运行 `build.ps1 -QtPrefix D:/msys64/mingw64 -CipherChatSource ../cipherchat`，需要匹配的 Qt 6、CMake、Ninja、OpenSSL 与 SQLite。服务端构建请参考 cipherchat 目录说明。

安装包见 [Releases](https://github.com/hajiujiu174/cipherchat/releases/latest)。第三方组件的来源、许可证及说明保留在 third_party 目录。

账号数据库、密钥、部署凭据、运行包和构建缓存不包含在源码中。
