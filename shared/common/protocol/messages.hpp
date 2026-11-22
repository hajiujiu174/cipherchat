#pragma once
// 应用层消息定义：客户端与服务器之间交换的 JSON 消息。
// 每条消息形如 {"type":"send_msg", ...}；type 之外的公共字段为 msg_id。
// 当前 NEXTALK 使用 ordinary-v1 + TLS：注册/登录不要求身份私钥，消息不是端到端密文。
// 以下旧版身份/预共享密钥字段及类型仅保留线格式兼容；当前路由明确拒绝预共享密钥接口。
//
// 消息一览（★=需登录）：
//   register / register_result                       普通账号注册（邮箱验证码）
//   login_request / login_result（保留旧挑战消息类型供兼容性测试）
//                                                     挑战-响应登录（口令证明）
//   upload_prekeys / upload_prekeys_result       ★   已停用的旧接口
//   fetch_bundle / bundle                        ★   已停用的旧接口
//   send_msg / recv_msg / msg_ack                ★   TLS 保护的普通消息中转与确认
//   file_ack                                     ★   文件分块逐块确认（接收方→发送方）
//   presence                                     ★   在线状态广播（服务器下发）
//   typing                                       ★   "正在输入"提示（服务器透传）
//   list_users / user_list                       ★   旧版全量列表（兼容保留）
//   list_contacts / contact_list                 ★   好友联系人列表
//   search_users / search_result                 ★   陌生人查询结果
//   add_friend / friend_result                   ★   建立好友关系
//   create_group / group_result / group_update   ★   群组元数据管理
//   error                                            服务器错误通知

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc::proto {

// ---- 消息类型 ----
enum class MsgType {
    Register,
    RegisterResult,
    LoginRequest,
    LoginChallenge,
    LoginResponse,
    LoginResult,
    UploadPrekeys,
    UploadPrekeysResult,
    FetchBundle,
    Bundle,
    SendMsg,
    RecvMsg,
    MsgAck,
    FileAck,
    Presence,
    Typing,
    ListUsers,
    UserList,
    CreateGroup,
    GroupResult,
    GroupUpdate,
    Error,
    Hello,
    HelloResult,
    RecipientAck,
    UpdateGet,
    UpdateResult,
    ListContacts,
    ContactList,
    SearchUsers,
    SearchResult,
    AddFriend,
    FriendResult,
    RespondFriend,
    LookupUid,
    UidResult,
    FileCache,
    FileCacheResult,
    RegistrationCode,
    RegistrationCodeResult,
    PasswordReset,
    PasswordResetResult,
    SocialRequest,
    SocialResult,
    EmailLogin,
    GameRelay,
};

// 注：不叫 toString——避免与 doctest 的非限定 stringify 经 ADL 串味。
const char* msgTypeName(MsgType type);
// 解析Msg类型，交给调用方区分有效结果与错误。
std::optional<MsgType> parseMsgType(std::string_view text);

// 消息载荷：type 之外的剩余字段。
// 各消息字段约定见 docs/protocol.md；构造助手保证写入端字段正确。
struct Message {
    MsgType type = MsgType::Error;
    std::string msgId;        // 端到端关联 ID（如 chat/file 消息的 ACK 关联）
    nlohmann::json body;      // 其余字段，均由助手函数填充/读取

    // 把 协议消息 转为 JSON 表示，与存储或传输格式对应。
    nlohmann::json toJson() const;
};

// 序列化为 JSON 文本（帧载荷）；失败（不应发生）时抛 nlohmann 异常。
std::string serialize(const Message& msg);

// 反序列化；type 未知或 JSON 非法时返回 nullopt。
std::optional<Message> deserialize(std::string_view text);
// 判断V2条件是否成立。
bool isV2(const Message&);
// 给协议消息加上版本 2 字段后返回。
Message withV2(Message);
// 构造握手问候的协议消息，填充该类型需要的字段。
Message makeHello();
// 构造握手问候结果的协议消息，填充该类型需要的字段。
Message makeHelloResult(bool ok);
// 构造LookupUID的协议消息，填充该类型需要的字段。
Message makeLookupUid(std::string_view username);
// 构造UID结果的协议消息，填充该类型需要的字段。
Message makeUidResult(std::string_view username, std::int64_t uid);
// 构造收件人签收回执的协议消息，填充该类型需要的字段。
Message makeRecipientAck(std::string_view messageId, std::string_view to);

// ---- 类型化字段读取（缺字段时返回默认值）----
std::string bodyStr(const nlohmann::json& body, const char* key,
                    std::string_view def = "");
// 返回协议消息使用的正文Int。
std::int64_t bodyInt(const nlohmann::json& body, const char* key,
                     std::int64_t def = 0);
// 返回协议消息使用的正文Bool。
bool bodyBool(const nlohmann::json& body, const char* key, bool def = false);

// ---- 构造助手：写端 ----

// 注册：提交账号、口令与身份公钥材料（均为 Base64）。
Message makeRegister(std::string_view username, std::string_view password,
                     std::string_view signPub, std::string_view dhPub,
                     std::string_view signedPrekey, std::string_view spkSig);
// 构造Register结果的协议消息，填充该类型需要的字段。
Message makeRegisterResult(bool ok, std::string_view error,
                           std::int64_t userId);

// 登录：request 仅带用户名；challenge 带随机 nonce 与 PBKDF2 参数；
// response 带口令哈希证明与 Ed25519 对 nonce 的签名。
Message makeLoginRequest(std::string_view username, std::string_view password = {});
// 构造登录挑战的协议消息，填充该类型需要的字段。
Message makeLoginChallenge(std::string_view nonceB64, std::string_view saltB64,
                           std::int64_t iterations);
// 构造登录证明响应的协议消息，填充该类型需要的字段。
Message makeLoginResponse(std::string_view username, std::string_view proofB64,
                          std::string_view nonceB64, std::string_view sigB64);
// 构造登录结果的协议消息，填充该类型需要的字段。
Message makeLoginResult(bool ok, std::string_view error, std::int64_t userId = 0);

struct OneTimePrekey {
    std::int64_t id = 0;
    std::string pub;  // Base64 X25519 公钥
};
// 构造上传预共享密钥的协议消息，填充该类型需要的字段。
Message makeUploadPrekeys(const std::vector<OneTimePrekey>& keys);
// 构造上传预共享密钥结果的协议消息，填充该类型需要的字段。
Message makeUploadPrekeysResult(bool ok, int count, std::string_view error);

// 构造Fetch交付包的协议消息，填充该类型需要的字段。
Message makeFetchBundle(std::string_view username);

// otp 为空表示对方一次性密钥已耗尽（会话仍可建立，弱化保护并提示）。
struct PrekeyBundle {
    std::string username;
    std::string signPub;
    std::string dhPub;
    std::string signedPrekey;
    std::string spkSig;
    std::optional<OneTimePrekey> otp;
};
// 构造交付包的协议消息，填充该类型需要的字段。
Message makeBundle(const PrekeyBundle& bundle);

// kind: "chat" | "file"；payload 为端到端密文信封（Base64）。
Message makeSendMsg(std::string_view msgId, std::string_view to,
                    std::string_view kind, std::string_view payload,
                    std::int64_t groupId = 0);
// 构造接收消息通知的协议消息，填充该类型需要的字段。
Message makeRecvMsg(std::string_view msgId, std::string_view from,
                    std::string_view to, std::string_view kind,
                    std::string_view payload, std::int64_t ts,
                    std::int64_t groupId = 0);
// 构造服务端受理回执的协议消息，填充该类型需要的字段。
Message makeMsgAck(std::string_view msgId, bool delivered);
// 构造文件块进度确认的协议消息，填充该类型需要的字段。
Message makeFileAck(std::string_view msgId, std::int64_t seq);

// 构造在线状态的协议消息，填充该类型需要的字段。
Message makePresence(std::string_view username, bool online);
// 构造正在输入To的协议消息，填充该类型需要的字段。
Message makeTypingTo(std::string_view to);        // 客户端→服务器
// 构造正在输入From的协议消息，填充该类型需要的字段。
Message makeTypingFrom(std::string_view from);    // 服务器→对端

// 构造列表用户的协议消息，填充该类型需要的字段。
Message makeListUsers();
// 构造用户列表的协议消息，填充该类型需要的字段。
Message makeUserList(const std::vector<std::pair<std::string, bool>>& users);
// 构造列表联系人的协议消息，填充该类型需要的字段。
Message makeListContacts();
// 构造联系人列表的协议消息，填充该类型需要的字段。
Message makeContactList(const std::vector<std::pair<std::string, bool>>& users);
// 构造搜索用户的协议消息，填充该类型需要的字段。
Message makeSearchUsers(std::string_view query, std::string_view after = {});
// 构造搜索结果的协议消息，填充该类型需要的字段。
Message makeSearchResult(const std::vector<std::pair<std::string, bool>>& users);
// 构造Add好友的协议消息，填充该类型需要的字段。
Message makeAddFriend(std::string_view username);
// 构造Respond好友的协议消息，填充该类型需要的字段。
Message makeRespondFriend(std::string_view username, bool accept);
// 构造好友结果的协议消息，填充该类型需要的字段。
Message makeFriendResult(bool ok, std::string_view error, std::string_view username = {});

// 构造Create群聊的协议消息，填充该类型需要的字段。
Message makeCreateGroup(std::string_view name,
                        const std::vector<std::string>& members);
// 构造群聊结果的协议消息，填充该类型需要的字段。
Message makeGroupResult(bool ok, std::string_view error, std::int64_t groupId,
                        std::string_view name,
                        const std::vector<std::string>& members);
// 构造群聊更新的协议消息，填充该类型需要的字段。
Message makeGroupUpdate(std::int64_t groupId, std::string_view name,
                        const std::vector<std::string>& members);

// 构造错误的协议消息，填充该类型需要的字段。
Message makeError(std::string_view code, std::string_view text);

}  // namespace cc::proto
