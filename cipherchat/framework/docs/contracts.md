# 接口契约：实现与验收约定

本文固定正式客户端与服务端的接口行为。M0—M6 已按任务逐项实现并登记验收，最新结果以 tasks.json 和项目根 docs/verification.md 为准。“现有 v1”节描述保留的回归入口；正式桌面客户端使用 v2。接口变更仍须同步修改调用点与双端测试。

## 1. 类型、错误与所有权

- C++20；核心公共接口只使用 STL、原 common DTO 和 vendored JSON。命名空间 ccfw，原算法保持 cc。
- 二进制复用 `cc::Bytes/BytesView`，仅 UTF-8 文本放 string 语义字段。Base64 转换只发生在线协议边界。
- `Result<T>` 成功必须显式 `success(value)`；失败必须 `failure(Error)`。没有默认成功构造。
- `optional` 空表示“合法缺省”，例如无待收消息；网络失败、损坏数据库不能返回一个空 optional 来冒充。
- 错误 message 不能放正文、口令、私钥、根密钥或文件密钥。调试原始消息也不能无差别写入注册口令。
- 对象由应用组合根拥有，注入引用在被注入对象销毁前有效。同一组服务只服务一个已选账号。
- 本地 AccountScope 包含服务器标识和用户名；同名不同服务器的数据不可互用。

## 2. 线程与关闭

| 层 | 执行位置 | 允许做什么 |
|---|---|---|
| GUI | Qt 主线程 | 收集输入，发命令，读取事件队列，更新控件 |
| application/services/store/session | 一个业务 worker | 串行状态变更、数据库事务、PBKDF2/加密、文件块推进 |
| 网络接收 | transport 内部线程（可选） | 帧解析后放入有界队列，记录网络错误 |
| 服务端 | 原网络线程 + 明确的路由锁 | 校验认证、存储、密文投递，禁止锁内无限阻塞 |

`ITransport::poll` 非阻塞；`tick` 最多处理 64 条，每次返回主循环。单次文件推进也只处理有限块，不在一次 tick 内读完整文件。

shutdown 顺序：停止接收新 GUI 命令 → 停业务循环 → transport close 停止网络并 join → 最后销毁服务/store → 销毁 UI。绝不在持有业务 mutex 时等待会反向获取同一锁的线程。close/shutdown 幂等，业务线程不得 join 自己。

资源限额（课程设计初始值）：帧上限复用 8 MiB，transport 队列最多 256 条且累计载荷至多 16 MiB，每个对端待建会话消息最多 64 条，群 2–64 人。触顶返回受控错误；具体限额变更必须连测试和文档一起改。

## 3. 现有 v1 线格式（读取代码所得）

4 字节大端长度 + UTF-8 JSON，type 使用小写，例如 register/login_request/send_msg/recv_msg。单帧上限 8 MiB。不得根据原方案大写示例造第二套枚举。

```json
{
  "type": "send_msg",
  "msg_id": "message-id",
  "to": "bob",
  "kind": "chat",
  "payload": "Base64(JSON of existing ratchet Envelope)",
  "group_id": 0
}
```

v1 信封字段来自 `Envelope::toJson`，包括 dh/pn/n/ik/ek/opk_id/ct，二进制字段为 Base64。服务端从已认证连接确定 from，不能信任客户端自报的 from。

旧 `msg_ack.delivered` 只表示服务器尝试在线投递，并非对端已经解密签收。不能把它直接映射成 RecipientReceived。

## 4. v2 迁移目标（M2-03/M4-01）

为可靠 ACK 和加密内层绑定增加协议版本，不默默让旧端误读新消息。M2-03 负责新增 `hello/hello_result` 类型；客户端 TLS 建立后、注册或登录前发 hello。

```json
{"type":"hello","version":2}
{"type":"hello_result","version":2,"ok":true}
{"type":"recipient_ack","version":2,"msg_id":"message-id","to":"alice"}
```

服务器不支持 v2/返回旧版未知类型时，新客户端明确报 ProtocolError；本任务不承诺 v1/v2 互通。v2 每条业务消息包含整数 version=2；服务器保存每连接协商版本。保留旧入口仅用于旧版回归。

recipient_ack 的发送者由已登录连接确定。服务端匹配 recipient+msg_id 后删除队列，通知原发送方；未知/不匹配不删。客户端只在认证解密且本地提交成功后 ACK；文件块 ACK 在块已验证落盘并提交 checkpoint 后发送。

v2 的 ratchet 算法/头结构仍复用原实现；把以下应用对象序列化为 UTF-8 后作为 ratchet 的明文输入：

```json
{
  "version": 2,
  "message_id": "message-id",
  "logical_group_message_id": "",
  "from": "alice",
  "to": "bob",
  "kind": "chat",
  "group_id": 0,
  "timestamp": 0,
  "body_base64": "base64 of UTF-8 text or binary metadata"
}
```

解密认证后，内层 message_id/from/to/kind/group_id 必须与外层一致；timestamp 是发送端时间，仅展示，不用于鉴权，服务器时间与之可不同。群逻辑 ID 在内层，群每个收件人有各自的外层 message_id。

JSON 采用 nlohmann 默认紧凑 dump；外层字符串长度、整数范围与消息类型必须严格验证，不能靠默认空字段继续执行。身份 AAD 仍由原 ratchet 接口提供，双方序列化规则一致。新内层不等于宣称实现 Signal 的完整标准格式。

## 5. 身份、X3DH、ratchet

GUI 迁移选项 `--account-scope` 可显式保留原账号的逻辑服务器标识，只影响本地账号目录和存储作用域，不改变网络目标、TLS 名称或证书验证。仅在界面连接参数与启动时的 host、port、CA 路径和 expectedServerName 完全一致时应用；用户改变连接参数后恢复默认隔离。未设置该选项时保持原来的 `host:port/expectedServerName` 规则。2026-09-15 新服务器全新部署包不设置此选项，使用新作用域与独立数据目录。

- 长期身份/一次性私钥只能保存在客户端；首次注册前就保存可恢复的本地身份，防止注册成功后崩溃丢失私钥。
- 登陆加载原私钥；不允许为已有账号悄悄生成新私钥。客户端注册经 TLS 向服务器提交账号口令，服务器处理认证；“只见密文”指聊天/文件载荷，不等于服务器对任何账号信息零可见。
- SPK 签名验证不能代替用户身份核对。框架必须保存签名公钥和 DH 身份公钥，未知/变化进入 IdentityChanged，通过明确确认入口 acceptPeerIdentity 处理。
- 同一束内的公钥长度/签名都要验证，不能只判断 Base64 非空。
- decrypt 在候选副本上执行，失败恢复原状态，包括 skipped keys 和 OPK。不要先消费一次性材料再验签。
- skipped key 总量保留上限 64；有 OPK/无 OPK 都测试。乱序窗口溢出是受控错误，不能无限扩容。
- 修复旧 decrypt 的副作用与 SessionService 的事务是两道不同边界：算法级保证失败不改状态，存储级保证崩溃后状态一致。

身份认证的区别参考 [Signal X3DH §4.1](https://signal.org/docs/specifications/x3dh/#authentication)：身份公钥还需通过可信方式核对。状态处理与缓存边界参考 [Signal Double Ratchet](https://signal.org/docs/specifications/doubleratchet/)。本工程是独立的课程设计简化协议，没有验证与 Signal 互操作。

## 6. 持久化与去重

当前选择一个受保护的完整 ClientSnapshot 作为原子单元，字段包括身份、会话、信任、历史、outbox、receivedMessages、pendingPayloads、transfers。实现从 schema/client.sql 起步。

- open 新库：revision=0、identity=nullopt；已有损坏库必须报错，不能自动清空。
- load：验证服务器/账号/格式版本，再通过 ISecretProtector 解封；任何错误返回 StorageFailure。
- commit(expected,next)：要求 next.revision=expected+1，BEGIN IMMEDIATE 内检查 expected，一次更新；失败回滚。
- 发送：候选棘轮 + history（chat 类型）+ outbox 原子保存，提交后才触网。
- 接收：候选棘轮 + history（chat 类型）+ 去重 + OPK 消耗一起保存，之后才向 UI 发布并 ACK。
- 非 chat 载荷解密后将明文（仍受本地快照加密保护）存入 pendingPayloads，与 ratchet 一起提交。文件服务消费它并提交 checkpoint/ACK 状态后，才能移除 pendingPayloads 并完成确认；崩溃恢复先处理暂存载荷，避免已消耗密钥却丢失文件元数据。
- receivedMessages 按账号隔离，同时保存消息 ID、双方、kind、groupId 与密文 SHA-256。完全匹配的已处理包返回 AlreadyProcessed，不再解密或展示；调用者可重发原签收。相同 ID 但绑定字段不同返回 ProtocolError。初始保留最近 4096 条；更旧重放仍由 ratchet 已消费的密钥拒绝。
- outbox 保存已生成密文，重发使用相同 ID/相同字节，不再次推进链。
- SQLite 明文中不能出现私钥、ratchet 状态、聊天正文、文件密钥。账号索引/元数据仍可见。
- DPAPI 只保护静态文件，不防当前登录用户或已被控制进程读取内存。整快照适合课程规模，大数据迁移另做，不在本次任务自行拆大工程。

## 7. 文件

本框架选用 **随机 32 字节 fileKey，经会话加密 file_meta 传递** 的现有路线。文件 key 不取自 ratchet 内部根密钥。

file_meta 解密后的 body 是 UTF-8 JSON，固定字段：`version=2,file_id,name,size_bytes,chunk_size=65536,chunk_count,key_base64,sha256,file_transport="window-cbor-v1",window=16`。file_id 使用 16 字节随机 ID 的 Base64 字符串，只作为标识，不用于本地文件路径。元数据的 size/chunk_count 必须一致，0 字节文件允许 chunk_count=0。发送端收到带同一模式与窗口的认证元数据 ACK 后才能发块；缺少声明、其他窗口或旧格式均拒绝，无回退路径。

chunk_count 用整数上取整，不要丢失短尾块。保留 nonce 与链式 AAD 的现有算法，但修正 openChunk 对短密文的拒绝。接收用 .part，按连续序号校验前一标签，随机 nonce 派生参数在同一 fileKey 下不可对不同内容复用。

外层 v2 的 file_chunk payload 是 `Base64("CCF3" + CBOR)`，CBOR 为恰好五字段的平面 map：`version=2,file_id,seq,cipher,tag`，后两者为原始二进制。只允许一次 Base64；旧 JSON 块解码器已移除。SAX 解析拒绝嵌套容器、重复键、未知键、错误类型、截断和尾随数据。file_id 必须匹配经过会话认证的元数据；外层消息 ID 为 `fchunk:<file_id>:<seq>`。服务器只转发不透明载荷，无须修改部署。

续传 checkpoint 保存密钥（受快照保护）、源摘要、连续块序号、前一标签、已验证 .part 长度及至多 16 个连续在途块的原密文和标签。重复 ACK/重复块幂等，错 peer/错 fileId/超出已持久化发送窗口的 seq 拒绝。file_ack 必须经过当前会话认证，内层绑定 file_id、seq、peer，并包含 `file_transport="window-cbor-v1",window=16`；服务端转发回执不推进文件字节。application 将其路由到 FileTransferService 进行会话解密后再调用 acknowledge。

接收缓存跨所有传输最多 64 块，单文件也不接受超出当前位置 64 块的序号。连续 4 块、尾块或等待 100 ms 后统一处理，单 tick 上限仍为 4 块。磁盘顺序：认证完整候选批次 → 一次写入并 flush → 快照提交 checkpoint → 加密累计 ACK。崩溃恢复按 checkpoint 截断额外尾部，最后验证 size+SHA-256，再原子改名为最终文件。下载路径基于本地指定目录，用安全 basename 与随机 ID 派生前缀；路径已存在就拒绝覆盖，从不接受远端绝对路径或 ../。

原函数的“已收到块数”不能直接塞给 FileProgress.authenticatedBytes；框架进度字段单位固定为字节。complete 只在完整长度/摘要验证及最终落盘成功后为 true。

M8-04：TransferCheckpoint/FileProgress 追加 timestamp（Unix 毫秒）。发送任务创建时保存时间；file_meta 沿用已有的加密内层 PlainEnvelope.timestamp 携带该时间，接收方仅在认证后保存。进度、续传、重登录均不更新时间。旧快照缺少 timestamp 时读取为 0，GUI 标注历史文件时间未记录，稳定排在已知时间之前；不拿磁盘修改时间或本次启动时间替代。此字段只用于显示，不用于鉴权，服务器及文件块格式不变。

### M4/M7 文件实现接口

FileTransferService 提供 setDownloadDirectory、tick、takeEvents，构造依赖不变。TransferCheckpoint 的单个 pendingChunk/pendingTag 已替换为 pendingChunks，新增 transportVersion=3；旧检查点缺省为 2，只保留读取、展示和取消，不能续传。旧文件的待发元数据、签收及块不会因重连重新发送，旧记录不占新活动传输名额；原身份、历史和会话可继续使用。FileProgress 的字节单位不变。

发送方向最多 16 个在途块，窗口原密文在触网前提交；超时从最早未确认块开始重发，初始 3 秒，退避上限 30 秒，进度更新后复位，计时使用单调时钟。全部新活动传输最多 16 个，单文件上限 1 GiB。源文件先流式哈希，Windows 发送期间持有拒绝写入/删除的只读句柄；进程恢复先重算摘要，变化则拒绝继续使用旧 key。file_ack 的 seq=4294967295 表示元数据已认证（空文件须已完成校验与改名）；其余 seq 表示已持久化的最后连续块序号。重复块重发已保存的累计 ACK 密文。取消保留快照及 .part，原传输不再恢复；重新发送会创建新 key 和 ID。

## 8. 群与输入提示

GroupService 持有当前 AccountScope，排除自己后逐成员调用 ChatService。GroupSendReport 每人一个结果，部分失败不是整体假成功。重试仅失败成员，逻辑群消息 ID 不变。

服务器校验 groupId 有效且连接身份/收件人都是成员。成员列表是服务器元数据，客户端不能自报一个 groupId 就发送给任意人。群快照加 revision，旧快照不得覆盖新状态。

M5 补充：GroupService 可注入 ILocalStore 持久化群快照与逐成员发送报告，新增 snapshots()/retry(logicalMessageId)。正式组合根必须传入存储；纯接口测试可省略。群草稿先持久化，单成员发送后保存结果；同一 logicalMessageId+groupId+recipient 的 ChatService 请求复用已保存的本地消息 ID，覆盖崩溃发生在两次提交之间的情形。服务端群表增加 revision（初始 1），登录后推送该用户的现有群快照。TypingEvent 新增 active 标志；应用使用单调时钟抑制重复提示、三秒到期，断线发布清空状态。

presence/typing 是可见元数据，不作为身份认证依据。typing 最快每秒一次，3 秒未更新过期；断线立即清空。

## 9. 明确的能力边界

M3 在原计划里提到明文打通；现有项目已经实现加密路径，框架不增加正式明文模式。传输测试可以使用纯测试消息，但不能把真实聊天正文送进服务器明文分支。

框架编译通过只说明接口可组装。M1 算法测试、M2 服务端测试、M3 headless、M4 文件/GUI、M5 三客户端与重启、M6 真机演示分别验收，不能用一个绿色 CTest 覆盖所有完成声明。

## 10. M8 混合文件通道（2026-09-16）

`DirectFileChannel::connect(endpoint)` 只启动异步尝试，成功返回不代表 TCP/握手已完成；以 `connected()/failed()` 观察结果。发送和接收均使用有界队列，单网络线程独占活跃 socket。`fileId()` 为业务线程检查通道归属；一个通道只承载绑定文件的 file_chunk/file_ack。旧同步调用方与验收已更新。

`FileTransferService::setDirectEnabled(bool)` 由业务线程调用；禁用会关闭当前通道，下次 tick 回退 Relay。重启后的 path=1/2 检查点不复用旧端点，重验文件并中转续传。其他并发文件不替换现有直连。`WindowOptions::directEnabled` 为空时读取 INI，有值时仅覆盖本次启动；Authenticate 包含实际选择，SetDirectEnabled 命令允许在传输中切换。无服务时该设置命令不报登录错误。

只有文件哈希完成、准备 metadata 时才创建监听。控制协议与 Relay window-cbor-v1 保持兼容；GUI、命令行和测试驱动同步更新。可用性由 M8-01 真实文件与回退用例、M8-preferences 及公网双 GUI 分别验收。

## 11. M9 客户端增量更新

M9-02 联系人扩展（追加枚举，不改变旧值）：list_contacts/contact_list 返回已确认好友及 incoming/outgoing 申请；add_friend 仅创建申请，respond_friend(username, accept) 以当前认证连接作为接收人处理。同意事务同时创建双向好友并移除双方申请，拒绝只移除该入站申请。search_users(query, after) 排除自己/好友，每页 50 人，search_result 回显 msg_id/query/after 并给出 next 游标，空 next 表示末页。新版仅在发现页请求陌生人，不在登录时拉全量用户。临时会话不改变关系；目录推送只发给已使用新目录接口的连接。公共在线元数据和旧 list_users 为旧版兼容保留，不把此 UI 分类描述为用户目录的访问控制。

原 MsgType 末尾追加 UpdateGet/UpdateResult，线格式 type 为 update_get/update_result；调用方是独立更新 helper 的 TlsTransport，服务端连接 I/O 线程直接处理，不进入 Router 或账号业务线程。只读 path=latest.json 或 blobs/<sha256>，响应回显 msg_id，ok=true 时 data 为 Base64 文件内容。每次请求只接收一个有界响应；普通消息发送期限保持 5 秒，更新响应允许 60 秒。客户端校验平台、序号、路径和每个内容块后才激活。

ccfw_updates 无 Qt/server 依赖，复用原 SHA-256 与 TLS 实现；GUI 通过 QProcess 驱动，BusinessWorker 的业务命令/事件和持久化契约不变。安装版本由根 current.txt 指向，previous.txt 用于显式回退。版本完整重建、当前版本不覆盖、用户目录不参与清单。完整设计、发布步骤和测试边界见 ../../docs/updates-20260919.md。

## 12. M10 功能扩展与恢复顺序（2026-10-08）

完整增量约定见 [M10 接口变更与兼容约定](features-contracts-20261007.md)。消息阶段来自实际提交、传输及回执，保留原 DeliveryState 含义；对端签收不表示已读。阶段缓存最多500条消息，每条32项，缺少观察时间显示未记录。

BusinessWorker 先发布登录账号的 RestoredView，再发布 Online。主窗口协调两个视图的 WorkspaceState，过期保存回执不覆盖最新选择或草稿。PeerIdentity 已核对回执只更新现有卡片；同一公钥指纹的自动提示去重，明确检查入口仍可打开卡片。确认命令重新检查两把公钥，公钥改变时禁止旧卡确认。

DeliveryService 编排已有文件服务，交付清单和人工确认存入加密 chat 可选扩展，普通正文保留可读说明；旧端继续接收说明和文件。扩展状态在同库 client_features 表内经 DPAPI 保护，与原 client_state 快照在同一 CAS 事务提交，旧快照格式不变。人工确认绑定接收方、交付ID、清单摘要和完整文件校验，与协议签收分开。

正式客户端继续由唯一业务线程拥有账号、会话和存储。2026-10-11按用户要求删除答辩演示入口、启动参数及专用演练组件，不再提供故障控制或投影隐私模式。正常聊天、文件交付、身份核对和双界面热切换的命令与数据格式保持不变。独立更新进程最多使用四个固定块下载线程，每路独立 TLS 和请求ID，网络错误有限重连，证书、协议和摘要错误直接拒绝；这不改变账号线程模型。

本批只交付本地包，公共服务器未修改。实际验证及独立包启动、激活和回退记录见 [M10 本地交付记录](../../docs/features-20261008.md)。
