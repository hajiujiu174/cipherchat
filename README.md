# CipherChat

CipherChat is a C++/Qt desktop chat application with a native client and server. This repository combines the two adjacent source trees used by the build: `cipherchat/` and `Messager/`.

当前版本支持普通聊天、用户名或邮箱密码登录、独立邮箱验证码登录、小程序广场、科学计算器和双人在线五子棋。在线传输使用 TLS；消息不使用端到端加密或用户身份私钥。

## Build

On Windows, install a matching Qt 6 and CMake/Ninja toolchain, then run `Messager\build.ps1 -QtPrefix <Qt-prefix>`. The script builds the desktop app with its native backend from the adjacent `cipherchat` directory. The native server can be built from `cipherchat` with CMake; see its README and presets.

Build outputs, local runtime profiles, releases, deployment snapshots, credentials, and server data are intentionally excluded. Configure SMTP using environment variables documented in the server source; never commit production credentials.

Third-party components retain their upstream license and attribution files.
