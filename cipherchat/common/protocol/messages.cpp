#include "common/protocol/messages.hpp"

namespace cc::proto {
namespace {

const char* kTypeNames[] = {
    "register",       "register_result",      "login_request", "login_challenge",
    "login_response", "login_result",         "upload_prekeys", "upload_prekeys_result",
    "fetch_bundle",   "bundle",               "send_msg",      "recv_msg",
    "msg_ack",        "file_ack",             "presence",      "typing",
    "list_users",     "user_list",            "create_group",  "group_result",
    "group_update",   "error", "hello", "hello_result", "recipient_ack", "update_get", "update_result",
    "list_contacts", "contact_list", "search_users", "search_result", "add_friend", "friend_result", "respond_friend", "lookup_uid", "uid_result",
    "file_cache", "file_cache_result", "registration_code", "registration_code_result", "password_reset", "password_reset_result", "social_request", "social_result", "email_login",
};

// 索引与 MsgType 枚举值一一对应
static_assert(sizeof(kTypeNames) / sizeof(kTypeNames[0]) ==
              static_cast<std::size_t>(MsgType::EmailLogin) + 1);

}  // namespace

const char* msgTypeName(MsgType type) {
    return kTypeNames[static_cast<std::size_t>(type)];
}

std::optional<MsgType> parseMsgType(std::string_view text) {
    for (std::size_t i = 0; i < sizeof(kTypeNames) / sizeof(kTypeNames[0]); ++i) {
        if (text == kTypeNames[i]) return static_cast<MsgType>(i);
    }
    return std::nullopt;
}

nlohmann::json Message::toJson() const {
    nlohmann::json j = body;
    j["type"] = msgTypeName(type);
    if (!msgId.empty()) j["msg_id"] = msgId;
    return j;
}

std::string serialize(const Message& msg) { return msg.toJson().dump(); }

bool isV2(const Message& message) {
    const auto it = message.body.find("version");
    return it != message.body.end() && it->is_number_integer() && *it == 2;
}
Message withV2(Message message) { message.body["version"] = 2; return message; }
Message makeHello() { return {MsgType::Hello, "", {{"version", 2}}}; }
Message makeHelloResult(bool ok) { return {MsgType::HelloResult, "", {{"version", 2}, {"ok", ok}}}; }
Message makeLookupUid(std::string_view username) { return {MsgType::LookupUid, "", {{"username", username}}}; }
Message makeUidResult(std::string_view username, std::int64_t uid) { return {MsgType::UidResult, "", {{"username", username}, {"uid", uid}}}; }
Message makeRecipientAck(std::string_view id, std::string_view to) {
    return {MsgType::RecipientAck, std::string(id), {{"version", 2}, {"to", to}}};
}

std::optional<Message> deserialize(std::string_view text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    auto typeIt = j.find("type");
    if (typeIt == j.end() || !typeIt->is_string()) return std::nullopt;
    auto type = parseMsgType(typeIt->get<std::string>());
    if (!type) return std::nullopt;
    Message msg;
    msg.type = *type;
    if (auto it = j.find("msg_id"); it != j.end()) {
        if (!it->is_string() || it->get_ref<const std::string&>().size() > 128) return std::nullopt;
        msg.msgId = it->get<std::string>();
    }
    j.erase("type");
    j.erase("msg_id");
    msg.body = std::move(j);
    return msg;
}

std::string bodyStr(const nlohmann::json& body, const char* key,
                    std::string_view def) {
    if (auto it = body.find(key); it != body.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return std::string(def);
}

std::int64_t bodyInt(const nlohmann::json& body, const char* key,
                     std::int64_t def) {
    if (auto it = body.find(key); it != body.end() && it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    return def;
}

bool bodyBool(const nlohmann::json& body, const char* key, bool def) {
    if (auto it = body.find(key); it != body.end() && it->is_boolean()) {
        return it->get<bool>();
    }
    return def;
}

namespace {

Message base(MsgType type) { return Message{type, "", nlohmann::json::object()}; }

}  // namespace

Message makeRegister(std::string_view username, std::string_view password,
                     std::string_view signPub, std::string_view dhPub,
                     std::string_view signedPrekey, std::string_view spkSig) {
    auto m = base(MsgType::Register);
    m.body["username"] = username;
    m.body["password"] = password;
    m.body["sign_pub"] = signPub;
    m.body["dh_pub"] = dhPub;
    m.body["signed_prekey"] = signedPrekey;
    m.body["spk_sig"] = spkSig;
    return m;
}

Message makeRegisterResult(bool ok, std::string_view error,
                           std::int64_t userId) {
    auto m = base(MsgType::RegisterResult);
    m.body["ok"] = ok;
    if (!error.empty()) m.body["error"] = error;
    m.body["user_id"] = userId;
    return m;
}

Message makeLoginRequest(std::string_view username) {
    auto m = base(MsgType::LoginRequest);
    m.body["username"] = username;
    return m;
}

Message makeLoginChallenge(std::string_view nonceB64, std::string_view saltB64,
                           std::int64_t iterations) {
    auto m = base(MsgType::LoginChallenge);
    m.body["nonce"] = nonceB64;
    m.body["salt"] = saltB64;
    m.body["iterations"] = iterations;
    return m;
}

Message makeLoginResponse(std::string_view username, std::string_view proofB64,
                          std::string_view nonceB64, std::string_view sigB64) {
    auto m = base(MsgType::LoginResponse);
    m.body["username"] = username;
    m.body["proof"] = proofB64;
    m.body["nonce"] = nonceB64;
    m.body["sig"] = sigB64;
    return m;
}

Message makeLoginResult(bool ok, std::string_view error, std::int64_t userId) {
    auto m = base(MsgType::LoginResult);
    m.body["ok"] = ok;
    if (ok && userId > 0) m.body["user_id"] = userId;
    if (!error.empty()) m.body["error"] = error;
    return m;
}

Message makeUploadPrekeys(const std::vector<OneTimePrekey>& keys) {
    auto m = base(MsgType::UploadPrekeys);
    auto arr = nlohmann::json::array();
    for (const auto& k : keys) {
        arr.push_back({{"id", k.id}, {"pub", k.pub}});
    }
    m.body["keys"] = std::move(arr);
    return m;
}

Message makeUploadPrekeysResult(bool ok, int count, std::string_view error) {
    auto m = base(MsgType::UploadPrekeysResult);
    m.body["ok"] = ok;
    m.body["count"] = count;
    if (!error.empty()) m.body["error"] = error;
    return m;
}

Message makeFetchBundle(std::string_view username) {
    auto m = base(MsgType::FetchBundle);
    m.body["username"] = username;
    return m;
}

Message makeBundle(const PrekeyBundle& bundle) {
    auto m = base(MsgType::Bundle);
    m.body["username"] = bundle.username;
    m.body["sign_pub"] = bundle.signPub;
    m.body["dh_pub"] = bundle.dhPub;
    m.body["signed_prekey"] = bundle.signedPrekey;
    m.body["spk_sig"] = bundle.spkSig;
    if (bundle.otp) {
        m.body["otp"] = {{"id", bundle.otp->id}, {"pub", bundle.otp->pub}};
    } else {
        m.body["otp"] = nullptr;
    }
    return m;
}

Message makeSendMsg(std::string_view msgId, std::string_view to,
                    std::string_view kind, std::string_view payload,
                    std::int64_t groupId) {
    auto m = base(MsgType::SendMsg);
    m.msgId = std::string(msgId);
    m.body["to"] = to;
    m.body["kind"] = kind;
    m.body["payload"] = payload;
    if (groupId > 0) m.body["group_id"] = groupId;
    return m;
}

Message makeRecvMsg(std::string_view msgId, std::string_view from,
                    std::string_view to, std::string_view kind,
                    std::string_view payload, std::int64_t ts,
                    std::int64_t groupId) {
    auto m = base(MsgType::RecvMsg);
    m.msgId = std::string(msgId);
    m.body["from"] = from;
    m.body["to"] = to;
    m.body["kind"] = kind;
    m.body["payload"] = payload;
    m.body["ts"] = ts;
    if (groupId > 0) m.body["group_id"] = groupId;
    return m;
}

Message makeMsgAck(std::string_view msgId, bool delivered) {
    auto m = base(MsgType::MsgAck);
    m.msgId = std::string(msgId);
    m.body["delivered"] = delivered;
    return m;
}

Message makeFileAck(std::string_view msgId, std::int64_t seq) {
    auto m = base(MsgType::FileAck);
    m.msgId = std::string(msgId);
    m.body["seq"] = seq;
    return m;
}

Message makePresence(std::string_view username, bool online) {
    auto m = base(MsgType::Presence);
    m.body["username"] = username;
    m.body["online"] = online;
    return m;
}

Message makeTypingTo(std::string_view to) {
    auto m = base(MsgType::Typing);
    m.body["to"] = to;
    return m;
}

Message makeTypingFrom(std::string_view from) {
    auto m = base(MsgType::Typing);
    m.body["from"] = from;
    return m;
}

Message makeListUsers() { return base(MsgType::ListUsers); }

Message makeUserList(const std::vector<std::pair<std::string, bool>>& users) {
    auto m = base(MsgType::UserList);
    auto arr = nlohmann::json::array();
    for (const auto& [name, online] : users) {
        arr.push_back({{"username", name}, {"online", online}});
    }
    m.body["users"] = std::move(arr);
    return m;
}

Message makeListContacts() { return base(MsgType::ListContacts); }

Message makeContactList(const std::vector<std::pair<std::string, bool>>& users) {
    auto m = base(MsgType::ContactList);
    auto arr = nlohmann::json::array();
    for (const auto& [name, online] : users) arr.push_back({{"username", name}, {"online", online}});
    m.body["users"] = std::move(arr); return m;
}

Message makeSearchUsers(std::string_view query, std::string_view after) {
    auto m = base(MsgType::SearchUsers); m.body["query"] = query; m.body["after"] = after; return m;
}

Message makeSearchResult(const std::vector<std::pair<std::string, bool>>& users) {
    auto m = base(MsgType::SearchResult);
    auto arr = nlohmann::json::array();
    for (const auto& [name, online] : users) arr.push_back({{"username", name}, {"online", online}});
    m.body["users"] = std::move(arr); return m;
}

Message makeAddFriend(std::string_view username) {
    auto m = base(MsgType::AddFriend); m.body["username"] = username; return m;
}

Message makeRespondFriend(std::string_view username, bool accept) {
    auto m = base(MsgType::RespondFriend); m.body["username"] = username; m.body["accept"] = accept; return m;
}

Message makeFriendResult(bool ok, std::string_view error, std::string_view username) {
    auto m = base(MsgType::FriendResult); m.body["ok"] = ok;
    if (!error.empty()) m.body["error"] = error;
    if (!username.empty()) m.body["username"] = username;
    return m;
}

Message makeCreateGroup(std::string_view name,
                        const std::vector<std::string>& members) {
    auto m = base(MsgType::CreateGroup);
    m.body["name"] = name;
    m.body["members"] = members;
    return m;
}

Message makeGroupResult(bool ok, std::string_view error, std::int64_t groupId,
                        std::string_view name,
                        const std::vector<std::string>& members) {
    auto m = base(MsgType::GroupResult);
    m.body["ok"] = ok;
    if (!error.empty()) m.body["error"] = error;
    if (ok) {
        m.body["group_id"] = groupId;
        m.body["revision"] = 1;
        m.body["name"] = name;
        m.body["members"] = members;
    }
    return m;
}

Message makeGroupUpdate(std::int64_t groupId, std::string_view name,
                        const std::vector<std::string>& members) {
    auto m = base(MsgType::GroupUpdate);
    m.body["group_id"] = groupId;
    m.body["revision"] = 1;
    m.body["name"] = name;
    m.body["members"] = members;
    return m;
}

Message makeError(std::string_view code, std::string_view text) {
    auto m = base(MsgType::Error);
    m.body["code"] = code;
    m.body["text"] = text;
    return m;
}

}  // namespace cc::proto
