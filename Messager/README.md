# CipherChat · Messager V0.3.14

旧版 `2026.10.17.1` / `2026.10.19.1` 的在线升级入口已补齐：检查更新应显示兼容版本 `2026.10.19.2`，安装后使用 Messager 0.3.14。后续发布须同步旧通道，见 [旧版升级修复与发布约定](../cipherchat/docs/legacy-update-bridge-20261019.md)。

V0.3.14 修复聊天列表无法同步他人头像：无需打开个人页，自动分批刷新聊天参与者的头像和昵称，列表、聊天顶部、消息及详情头像原地更新；清空头像后恢复占位。保留草稿、选区和阅读位置，支持默认及彩铅皮肤。当前入口 `runtime-v0.3.14/cipherchat-launcher.exe`。见 [实现与验收](../cipherchat/docs/avatar-sync-20261019.md)。

V0.3.13 将“彩铅手绘 · 云朵气泡”皮肤重新适配到当前 Qt Widgets 界面。设置 → 外观 → 界面皮肤可选择彩铅或默认；奶油纸纹、楷体、云朵消息和手绘边框覆盖聊天、登录、文件、资料及朋友圈，支持浅色、深色和跟随系统。选择自动保存，切换时保留草稿、选区和聊天阅读位置。当时入口 `runtime-v0.3.13/cipherchat-launcher.exe`。

V0.3.12 按参考浅紫聊天标志统一登录、主界面品牌、窗口和Windows程序图标。当时入口runtime-v0.3.12/cipherchat-launcher.exe，保留自动更新。见 [资源与交付验收](../cipherchat/docs/brand-icons-20261018.md)。

V0.3.11 启动后在登录页自动检查新版本，发现更新时主动提示，无需先登录。点击下载更新，完成后退出并安装；原账号与聊天数据保留，无更新或检查失败仍可正常登录。通过稳定启动器跟随当前版本指针。当时完整目录 `runtime-v0.3.11`，旧电脑先解压本次完整包一次，之后即可自动检查。见 [在线更新验收](../cipherchat/docs/login-update-20261018.md)。

V0.3.10 左侧新增“朋友圈”，汇总自己和所有好友的动态并按发布时间逆序排列，支持刷新、加载更早动态、进入作者资料。个人资料页下方新增“发消息”，直接进入与对方的聊天。当时默认目录 `runtime-v0.3.10`，旧版与账号资料保留。见 [实现与验证](../cipherchat/docs/moments-feed-20261018.md)。

V0.3.9（2026-10-18）加入个人资料和文字朋友圈。点击左下角头像、联系人旁“资料”或聊天顶部头像/昵称打开个人页；上方是头像和昵称，下方按时间倒序显示朋友圈。自己的页面可更换头像、编辑昵称、发布最多2000字的文字，仅自己和已同意的好友可查看动态。资料、头像和动态保存到服务器，重启后保持。当时运行目录为 `runtime-v0.3.9`，V0.3.8 与原账号资料保留。见 [实现、权限与部署验收](../cipherchat/docs/profile-moments-20261018.md)。

V0.3.8 已上线“忘记密码”和已绑定邮箱验证码重置，当时运行目录为 `runtime-v0.3.8`。Linux、本地真实 TLS、邮件与控件联调及公网检查通过，用户确认收到真实重置邮件。未绑定邮箱的旧账号不能通过任意邮箱找回，原身份私钥仍须保留。见 [实现与部署验收](../cipherchat/docs/password-reset-20261018.md)。

V0.3.7（2026-10-18）新增注册邮箱、六位验证码与发送倒计时，当时运行包为 `runtime-v0.3.7/Messager.exe`。服务器已升级并接入 QQ SMTP，用户已确认收到真实测试邮件；已有账号登录无需补邮箱。实现、测试和部署证据见 [注册邮箱说明](../cipherchat/docs/registration-email-20261018.md)。

Messager 现在是 CipherChat 的 Qt Widgets 客户端界面，正常启动使用现有 CipherChat 原生业务层。登录、好友、私聊、群聊、身份核对、文件与历史记录均已接线。`--demo` 单独保留 V0.1 本地演示。

源码工程：`D:\Work\WORKDONE\Messager`。后端源码：`D:\Work\WORKDONE\cipherchat`。Messager 已替代 Codex，作为 CipherChat 默认和后续维护的主界面；桌面、开始菜单及 `cipherchat\启动CipherChat.bat` 均直接进入 Messager。Codex 已从当前界面选项中移除，旧源码和历史发行包仅留作归档；Rhine 保留独立入口。

## 运行

Windows 本地目录双击 `run.cmd`，或运行 `runtime-v0.3.14/cipherchat-launcher.exe`，运行库随目录提供，无需安装 Qt。桌面和开始菜单的 CipherChat 通过稳定启动器直接打开 Messager V0.3.14，启动时不显示界面选择窗口。原 `runtime-v0.3.7` 等版本保留用于回退；历史便携包中的 `runtime` 对应其发行日期。

V0.3.6 创建群聊的成员选择改为下拉菜单与复选框，使用当前账号的好友名单。支持搜索、多选、取消勾选和收起后保留选择，显示已选人数；自己自动加入，不重复出现在列表。群名为空或没有选中成员时禁用创建按钮。最多勾选 63 位好友，符合包含创建者在内 64 人的群组上限。列表没有好友时提示先添加好友；定时联系人刷新保持已选项，已被移除的好友不再提交。

V0.3.5 文件卡片新增“对话位置”和“在资源管理器中打开”。前者通过本次传输对应的消息 ID 跳转并短暂高亮，自动清除会隐藏目标的消息筛选；后者在 Windows 打开所在文件夹并选中文件。文件未保存或已移走时，如原文件夹仍在则打开该文件夹。没有可用位置时按钮禁用；Linux 打开所在文件夹。

V0.3.4 查询结果中，好友申请成功后按钮显示“已申请”并禁用，旁边可开启“临时会话”。临时会话仅限文字，附件按钮禁用；服务层也拦截向非好友发送文件及恢复上传。对方同意后保留原会话和消息，并允许发送文件。申请失败或被拒绝后可以重试；已是好友的结果显示“已是好友”和“发消息”。

V0.3.3 移除聊天输入区下方仅插入固定表情的按钮，保留附件与发送操作。

V0.3.2 修复聊天区周期性闪到顶部再返回的问题：无变化的联系人 / 消息刷新保留现有消息区；同一会话刷新不强制滚到底；消息确有更新时在同一绘制周期内恢复位置，移除延迟回滚。上翻阅读保留可见消息，停在底部时跟随新消息。

1. 启动后只显示独立登录窗口，输入现有 CipherChat 用户名与密码；成功后才显示聊天主界面。点击“注册账号”切换注册页，填写用户名、密码、确认密码和邮箱，发送验证码后填入六位数字并注册。验证码 10 分钟有效，60 秒后可重发；重发作废旧码。
2. “记住账号 / 记住密码”默认关闭；勾选后仅在认证成功时保存。Windows 密码由当前用户的 DPAPI 加密；取消勾选立即清除对应记住信息。连接参数收在“连接设置”独立窗口中。
3. 联系人页查询用户名、发送或处理好友申请，点击“临时会话”可在成为好友前发送文字；好友点击“发消息”，也可创建群聊。
4. 首次通信可能要求核对双方身份指纹。通过可信渠道比对后手动勾选并确认；未确认时保留后端待发状态。
5. Enter 发送，Shift + Enter 换行。回形针给当前私聊好友发送文件；文件页查看真实进度、恢复历史未完成项、取消或打开完成文件。临时会话不能发送文件。

当前运行包沿用原生客户端启动配置：服务器 `[2603:1040:f02:d::21d]:55220`，证书主机名为相同 IPv6 地址，使用随包公开服务器证书。默认账号根目录 `%LOCALAPPDATA%\CipherChat\remote-server2`，也支持 `CIPHERCHAT_DATA_DIR`。实际账号隔离、存储路径、私钥及信任记录由 CipherChat 原生 runtime 管理。请关闭同账号的其他客户端再使用同一资料目录。

不会保存明文密码到界面配置或日志；当前连接表单修改仅在本次运行生效。可编辑 `runtime/connection.json` 持久设置非密码连接项，或通过启动参数指定。不要在该文件存放密码。

```powershell
.\run.cmd --host 127.0.0.1 --port 55220 --server-name localhost --ca D:/server/cert.pem --data-dir D:/test-account-data
.\run-demo.cmd
```

默认窗口 1280 × 800，最小 960 × 640，均为 Qt 逻辑像素；保留系统标题栏。左侧导航、会话分栏、舒适密度、浅色 / 深色 / 跟随系统、字号、草稿、搜索、详情等 V1.1 布局继续使用。

## 已接入功能

| 界面操作 | 实际行为 |
|---|---|
| 登录、注册、退出 | BusinessWorker / ClientApplication 原生账号流程、TLS 校验与资料恢复 |
| 联系人 | 服务器用户查询及分页、好友申请 / 同意 / 拒绝、已申请状态、在线状态、文字临时会话 |
| 私聊 | 原生消息 ID、待发队列、服务器接收、对端签收、失败重试 |
| 群聊 | 原生群创建、成员、群消息与按成员签收统计；合并逻辑消息避免重复气泡 |
| 身份 | 本账号指纹、对端指纹展示、手动核对与原生确认命令 |
| 文件 | 真实路径发送、已验证字节进度、完成 / 取消、历史恢复、打开本地完成文件 |
| 重登 | 本机账号身份、信任记录、私聊 / 群聊历史、文件检查点 |
| 通知 | 当前账号的消息、文件、好友及错误活动；查看状态仅为本地界面状态 |

“对端签收”表示通信层确认，不表示已读。真实模式隐藏模拟消息和模拟失败按钮；传输中不提供后端不存在的暂停命令。文件发送面向好友私聊，临时会话与群聊附件入口禁用。

## 构建

完整客户端要求 CMake 3.24+、Qt 6.5+ Widgets、C++20 编译器、OpenSSL、SQLite；测试需要 Qt Test。UI / Controller / Model 使用 C++17，原生桥接层与现有后端使用 C++20。不会自动下载依赖。

本机验证环境：Windows 11 x64、Qt 6.11.2、GNU 16.2、Ninja。

```powershell
.\build.ps1 -QtPrefix D:/msys64/mingw64 -CipherChatSource D:/Work/WORKDONE/cipherchat
.\deploy.ps1 -QtPrefix D:/msys64/mingw64
.\run.cmd
```

```sh
cmake -S . -B work/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt -DCIPHERCHAT_SOURCE_DIR=/path/to/cipherchat -DBUILD_TESTING=ON
cmake --build work/build --target Messager messager_tests cipherchat_adapter_tests messager_live_tests
ctest --test-dir work/build --output-on-failure
```

仅构建演示可使用 `build.ps1 -DemoOnly`，或 `-DMESSAGER_WITH_CIPHERCHAT=OFF`。源码包通过 CIPHERCHAT_SOURCE_DIR 引用现有 CipherChat 源码，不重复打包整个后端。Linux 构建需要 Linux Qt 和原生依赖，未在 Linux 真机运行验收。

## 结构

```text
Messager/
├─ CMakeLists.txt / build.ps1 / deploy.ps1
├─ run.cmd / run-demo.cmd
├─ src/main.cpp                  默认组合真实服务；--demo 显式演示
├─ src/core/types.h              界面数据
├─ src/controllers/              操作与会话选择
├─ src/models/                   独立会话快照与筛选
├─ src/services/
│  ├─ chatservice.h              可替换服务契约
│  ├─ cipherchatservice.*        真实事件投影、账号及业务操作
│  ├─ cipherchatgateway.*        BusinessWorker 队列 / 事件桥接
│  ├─ cipherchatmapping.*        原生命令与状态转换
│  └─ mockservice.*              显式演示模式
├─ src/ui/                       聊天、页面、账号、身份、主题与组件
├─ tests/                        UI、原生契约、真实 TLS 双客户端联调
├─ docs/                         设计规范及接入说明
├─ screenshots/                  交付包内的最新原生 Qt 截图
├─ evidence/auth-native-v0.3/            真实本机服务联调截图与数据
├─ evidence/                     日志、验证和运行库依赖
├─ runtime/ 或 runtime-v0.3.6/     Windows 便携文件、公开服务器证书
└─ work/                         构建、V0.1 备份及隔离测试资料，不打包
```

## 验证与边界

群聊成员选择验证见 evidence/GROUP_PICKER_V0.3.6.md；文件定位与资源管理器验证见 evidence/FILE_ACTIONS_V0.3.5.md；好友申请与临时会话验证见 evidence/FRIEND_TEMP_V0.3.4.md；滚动修复实测见 evidence/SCROLL_FIX_V0.3.2.md；登录修复记录见 evidence/AUTH_FIX_V0.3.1.md。V0.3 界面记录保留于 evidence/VALIDATION.md，真实联调程序直接使用现有 CipherChat TLS 服务器和原生业务 worker，两端为独立本机测试账号，数据隔离于 work。交付包不含测试账号库、测试私钥、现有用户资料或密码。

已检查远程服务器 TLS 可达和证书主机名匹配；没有代替用户登录远程账号。远程真实账号、跨机器通信、P2P 直连、Linux 真机尚未验收。当前文件模式与原启动器一致，使用服务器中转。

尚未覆盖：托盘 / 系统通知、拖放及图片视频预览、群管理完整页面、大量消息虚拟列表、完整无障碍专项测试。当前活动通知仅保存于本次会话；消息与文件历史由原生持久层恢复。

运行库采用动态链接，许可文本位于运行目录 licenses。work/backup-v0.1 和旧版交付 ZIP 保留初版材料。




