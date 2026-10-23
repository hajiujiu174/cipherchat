# CipherChat 原生接入 V0.2

## 数据流

QWidget → AppController → CipherChatService → CipherChatGateway → BusinessWorker → ClientApplication

网关每 30 ms 在 Qt 主线程取出 worker 事件。worker 执行网络、加密、账号和存储工作，界面不重写密码学、协议或数据库。直接链接当前 ../cipherchat/framework 的 ccfw_rhine_json / ccfw_client_runtime，构建目录只写入 Messager 的 work/build。

CipherChatService 将恢复视图、联系人、身份、消息、群消息报告、文件检查点和账号阶段映射为界面数据。主程序默认注入这个服务；--demo 注入 MockService。

## 契约处理

- 重用 commandFromJson / eventJson，单独序列化 CommandResult 保留 requestId 与 operationId。
- 私聊在首次排队前生成原生 messageId，重试沿用同一个 ID，由现有业务层去重。
- 群聊以 logicalGroupMessageId 合并各成员记录，显示原生成员及投递统计。
- 身份确认携带当前 signing / dh 字段；确认前检查指纹资料未发生变化。取消核对可稍后在会话详情重新打开。
- TLS 使用 CA 文件和 serverName 校验；不提供跳过校验入口，不内置密码。
- 真实文件传入完整路径，进度取 authenticatedBytes / totalBytes；完成、取消取后端标记。历史未完成检查点映射为“可恢复”。
- 未实现的 pauseFile 不会提交；群文件发送入口禁用；已取消项不自动重新发送。
- 会话模型保留自身快照，在退出 / 恢复改变服务容器时继续提供有效的模型行，按 Qt 模型通知顺序更新。
- 退出时清除显示数据、草稿、文件卡片和本次活动，底层账号资料保留给原生 runtime。

## 与现有客户端并存

原 CipherChat 的 GUI 源码、Rhine 页面、通信核心和启动脚本没有被替换。只新增根目录 启动CipherChat-Messager.bat，调用相邻 Messager 的 run.cmd。

运行包复制原启动器使用的公开服务器证书及非密码连接信息。资料根目录、服务器地址、端口、证书主机名与 accountScope 由原生 AccountCoordinator 参与账号隔离；使用原账号资料时应保持这些字段匹配。不要同时用两个客户端写同一账号资料目录。

支持 --host、--port、--server-name、--ca、--data-dir、--account-scope、--username；也支持原有 CIPHERCHAT_DATA_DIR。没有密码命令行参数。表单中的连接修改仅在当前进程保留。

## 验证办法

tests/tst_live.cpp 直接启动现有服务器源码组成的 loopback TLS 服务，生成隔离证书 / 数据目录，运行两个 Widgets 窗口和两个真实服务。测试注册、好友、指纹比对、双向文本、多行输入、群消息、二进制文件、断线续传、身份和历史恢复，以及错误密码 / 错误证书主机名拒绝。截图和校验摘要写入 MESSAGER_EVIDENCE_DIR；临时私钥和数据库留在 work，不进入包。

自动测试中身份指纹通过另一端实际公开指纹作比对后确认，生产界面仍要求用户手动核对。测试没有创建远程账号或读取现有账号密码。

## 后续集成点

1. 持久保存非密码连接配置及多个服务器资料切换。
2. 群成员变更、离群等完整管理页面，对接现有业务命令后再开放。
3. 后端文件失败事件的细分类、断线自动重连和恢复策略。
4. 图片视频预览、拖放、系统通知与托盘。
5. 跨机器中转 / P2P、Linux、同版本旧资料迁移与长历史性能专项验收。

本版不把本机联调等同于公网账号验收，也没有替原 CipherChat 做完整业务回归。


## V0.3 账号入口

登录和注册改为独立的 530 px 宽入口窗口，使用参考图的锁形标志、品牌标题、表单、记住选项和紫色主按钮。首次启动不显示主界面；成功认证时先显示主窗口，再关闭账号窗口，避免最后一个窗口关闭而意外退出。错误和校验失败保留在账号页；退出账号后隐藏聊天主窗口并返回登录。

注册要求两次密码一致；连接参数仅按需在单独窗口中显示。账号页支持原有浅 / 深色主题。Windows 的“记住密码”使用当前用户的 CryptProtectData / CryptUnprotectData，受服务器、证书、账号目录与用户名作用域约束；默认不启用，不自动登录，不把明文口令写入设置。未配置安全存储的平台禁用记住密码。

系统 API 说明：https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata

## V0.3.1 登录修复（2026-10-18）

修复打开连接设置后直接取消会将缺失的可选 accountScope 字段写成 JSON null 的问题。后端允许该字段缺失或为空字符串，但拒绝 null；原始登录因此在本地解析时失败。界面读取改为非修改访问，并在提交时移除可选 null，避免影响默认账号作用域及记住选项的绑定。用户可见错误提示改为登录 / 连接配置的说明。

补充回归：打开连接设置后取消，再注册、正确密码登录、错误密码登录；并验证真实命令解析器对缺失 / null / 空字符串三种值的契约。测试使用隔离的本机 TLS 服务与测试账号。
