#pragma once
// 应用层消息定义：客户端与服务器之间交换的 JSON 消息。
// 每条消息形如 {"type":"send_msg", ...}；type 之外的公共字段为 msg_id。
//
// 消息一览（★=需登录）：
//   register / register_result                       注册（含身份公钥与签名预共享密钥）
//   login_request / login_challenge / login_response / login_result
//                                                     挑战-响应登录（口令证明 + Ed25519 挑战签名）
//   upload_prekeys / upload_prekeys_result       ★   补充一次性预共享密钥
//   fetch_bundle / bundle                        ★   拉取对方公钥束（建立会话用）
//   send_msg / recv_msg / msg_ack                ★   密文中转（服务器不可读）
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
};

// 注：不叫 toString——避免与 doctest 的非限定 stringify 经 ADL 串味。
const char* msgTypeName(MsgType type);
std::optional<MsgType> parseMsgType(std::string_view text);

// 消息载荷：type 之外的剩余字段。
// 各消息字段约定见 docs/protocol.md；构造助手保证写入端字段正确。
struct Message {
    MsgType type = MsgType::Error;
    std::string msgId;        // 端到端关联 ID（如 chat/file 消息的 ACK 关联）
    nlohmann::json body;      // 其余字段，均由助手函数填充/读取

    nlohmann::json toJson() const;
};

// 序列化为 JSON 文本（帧载荷）；失败（不应发生）时抛 nlohmann 异常。
std::string serialize(const Message& msg);

// 反序列化；type 未知或 JSON 非法时返回 nullopt。
std::optional<Message> deserialize(std::string_view text);
bool isV2(const Message&);
Message withV2(Message);
Message makeHello();
Message makeHelloResult(bool ok);
Message makeLookupUid(std::string_view username);
Message makeUidResult(std::string_view username, std::int64_t uid);
Message makeRecipientAck(std::string_view messageId, std::string_view to);

// ---- 类型化字段读取（缺字段时返回默认值）----
std::string bodyStr(const nlohmann::json& body, const char* key,
                    std::string_view def = "");
std::int64_t bodyInt(const nlohmann::json& body, const char* key,
                     std::int64_t def = 0);
bool bodyBool(const nlohmann::json& body, const char* key, bool def = false);

// ---- 构造助手：写端 ----

// 注册：提交账号、口令与身份公钥材料（均为 Base64）。
Message makeRegister(std::string_view username, std::string_view password,
                     std::string_view signPub, std::string_view dhPub,
                     std::string_view signedPrekey, std::string_view spkSig);
Message makeRegisterResult(bool ok, std::string_view error,
                           std::int64_t userId);

// 登录：request 仅带用户名；challenge 带随机 nonce 与 PBKDF2 参数；
// response 带口令哈希证明与 Ed25519 对 nonce 的签名。
Message makeLoginRequest(std::string_view username);
Message makeLoginChallenge(std::string_view nonceB64, std::string_view saltB64,
                           std::int64_t iterations);
Message makeLoginResponse(std::string_view username, std::string_view proofB64,
                          std::string_view nonceB64, std::string_view sigB64);
Message makeLoginResult(bool ok, std::string_view error, std::int64_t userId = 0);

struct OneTimePrekey {
    std::int64_t id = 0;
    std::string pub;  // Base64 X25519 公钥
};
Message makeUploadPrekeys(const std::vector<OneTimePrekey>& keys);
Message makeUploadPrekeysResult(bool ok, int count, std::string_view error);

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
Message makeBundle(const PrekeyBundle& bundle);

// kind: "chat" | "file"；payload 为端到端密文信封（Base64）。
Message makeSendMsg(std::string_view msgId, std::string_view to,
                    std::string_view kind, std::string_view payload,
                    std::int64_t groupId = 0);
Message makeRecvMsg(std::string_view msgId, std::string_view from,
                    std::string_view to, std::string_view kind,
                    std::string_view payload, std::int64_t ts,
                    std::int64_t groupId = 0);
Message makeMsgAck(std::string_view msgId, bool delivered);
Message makeFileAck(std::string_view msgId, std::int64_t seq);

Message makePresence(std::string_view username, bool online);
Message makeTypingTo(std::string_view to);        // 客户端→服务器
Message makeTypingFrom(std::string_view from);    // 服务器→对端

Message makeListUsers();
Message makeUserList(const std::vector<std::pair<std::string, bool>>& users);
Message makeListContacts();
Message makeContactList(const std::vector<std::pair<std::string, bool>>& users);
Message makeSearchUsers(std::string_view query, std::string_view after = {});
Message makeSearchResult(const std::vector<std::pair<std::string, bool>>& users);
Message makeAddFriend(std::string_view username);
Message makeRespondFriend(std::string_view username, bool accept);
Message makeFriendResult(bool ok, std::string_view error, std::string_view username = {});

Message makeCreateGroup(std::string_view name,
                        const std::vector<std::string>& members);
Message makeGroupResult(bool ok, std::string_view error, std::int64_t groupId,
                        std::string_view name,
                        const std::vector<std::string>& members);
Message makeGroupUpdate(std::int64_t groupId, std::string_view name,
                        const std::vector<std::string>& members);

Message makeError(std::string_view code, std::string_view text);

}  // namespace cc::proto
