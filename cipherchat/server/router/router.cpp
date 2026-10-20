#include "server/router/router.hpp"

#include "common/crypto/crypto.hpp"
#include "server/auth/auth.hpp"
#include "common/auth/email.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <utility>

namespace cc::server {

namespace {

using cc::proto::bodyStr;

bool isValidUsername(std::string_view name) {
    if (name.size() < 2 || name.size() > 32) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               static_cast<unsigned char>(c) >= 0x80;  // 允许中文用户名
    });
}

bool isNonEmptyB64(std::string_view s, std::size_t maxLen) {
    if (s.empty() || s.size() > maxLen) return false;
    cc::Bytes tmp;
    return cc::base64Decode(s, tmp);
}

}  // namespace

Router::Router(Storage& db, Clock clock)
    : db_(db), clock_(clock ? std::move(clock) : Clock{[] {
          return std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
      }}) {}

std::string Router::connectionId(const Peer& peer) {
    // Peer 的生存期覆盖整条连接；onDisconnect 在网络层销毁 Peer 前执行。
    return std::to_string(reinterpret_cast<std::uintptr_t>(&peer));
}

void Router::onMessage(Peer& peer, const cc::proto::Message& msg) {
    std::unique_lock<std::mutex> lock(routeMutex_);
    using cc::proto::MsgType;
    if (msg.type == MsgType::Hello) {
        const bool ok = cc::proto::isV2(msg) && peer.username().empty() &&
            versions_.find(&peer) == versions_.end();
        if (ok) versions_[&peer] = 2;
        else if (!versions_.count(&peer)) versions_[&peer] = 0;
        auto response = cc::proto::makeHelloResult(ok); response.body["uid_lookup"] = ok; response.body["chat_mode"] = "ordinary-v1";
        response.body["file_cache"] = ok && fileCacheEnabled_;
        response.body["profile_moments"] = ok;
        response.body["moments_feed"] = ok;
        response.body["registration_email"] = ok && bool(registrationEmail_);
        response.body["registration_mail_available"] = ok && registrationEmail_ && registrationEmail_->available();
        response.body["password_reset_email"] = ok && bool(passwordResetEmail_);
        response.body["email_code_login"] = ok && bool(emailLogin_);
        if (ok && msg.body.contains("login_identifier")) {
            const auto account=db_.findAccountByLogin(bodyStr(msg.body,"login_identifier"));
            response.body["login_username"]=account ? account->username : "";
        }
        deliver(peer, std::move(response));
        return;
    }
    if ((usesV2(peer) && !cc::proto::isV2(msg)) ||
        (!usesV2(peer) && (msg.body.contains("version") || msg.type == MsgType::RecipientAck)) ||
        (versions_.count(&peer) && versions_.at(&peer) == 0)) {
        deliver(peer, cc::proto::withV2(cc::proto::makeError("protocol_error", "协议版本不匹配，请重新连接")));
        return;
    }
    // 收到业务消息后冻结 v1，不允许登录挑战中途升级协议。
    if (!versions_.count(&peer)) versions_[&peer] = 1;
    if (msg.type == MsgType::RegistrationCode) {
        const bool reset = bodyStr(msg.body, "purpose") == "password_reset";
        const bool login=bodyStr(msg.body,"purpose")=="email_login";
        auto mail = login ? emailLogin_ : reset ? passwordResetEmail_ : registrationEmail_;
        const auto email = bodyStr(msg.body,"email");
        const auto loginAccount=login ? db_.findAccountByLogin(email) : std::optional<Storage::Account>{};
        const auto user=loginAccount ? loginAccount->username : bodyStr(msg.body,"username");
        const bool allowed = usesV2(peer) && peer.username().empty() && isValidUsername(user);
        const bool bound = (!login || (loginAccount && db_.registrationEmail(loginAccount->username)==auth::normalizedEmail(email))) && (!reset || (!auth::normalizedEmail(email).empty() && db_.registrationEmail(user) == auth::normalizedEmail(email)));
        lock.unlock();
        RegistrationEmail::Result result{false, "当前服务器不支持邮箱验证码，请联系管理员"};
        if (!allowed) result.error = "请填写有效用户名，并在登录前获取验证码";
        else if (!bound) result.error = "用户名与已验证邮箱不匹配，或账号尚未绑定邮箱";
        else if (mail) {
            try { result = mail->request(user, email); }
            catch (...) { result = {false, "验证码请求未完成，请稍后重试"}; }
        }
        peer.deliver(cc::proto::withV2({MsgType::RegistrationCodeResult, msg.msgId,
            {{"ok", result.ok}, {"error", result.error}, {"retry_after", result.retryAfter},
             {"expires_in", result.ok ? 600 : 0}, {"email", email}}}));
        return;
    }
    try {
    switch (msg.type) {
        case MsgType::PasswordReset: handlePasswordReset(peer, msg); break;
        case MsgType::Register: handleRegister(peer, msg); break;
        case MsgType::LoginRequest: handleLoginRequest(peer, msg); break;
        case MsgType::LoginResponse: handleLoginResponse(peer, msg); break;
        case MsgType::EmailLogin: handleEmailLogin(peer,msg);break;
        case MsgType::UploadPrekeys: deliver(peer,cc::proto::makeError("unsupported","普通聊天不使用预共享密钥")); break;
        case MsgType::FetchBundle: deliver(peer,cc::proto::makeError("unsupported","普通聊天不使用公钥束")); break;
        case MsgType::LookupUid: handleLookupUid(peer, msg); break;
        case MsgType::SendMsg: handleSendMsg(peer, msg); break;
        case MsgType::FileAck: handleFileAck(peer, msg); break;
        case MsgType::FileCache: handleFileCache(peer, msg); break;
        case MsgType::SocialRequest: handleSocial(peer,msg); break;
        case MsgType::RecipientAck: handleRecipientAck(peer, msg); break;
        case MsgType::Typing: handleTyping(peer, msg); break;
        case MsgType::ListUsers: handleListUsers(peer); break;
        case MsgType::ListContacts: handleListContacts(peer); break;
        case MsgType::SearchUsers: handleSearchUsers(peer, msg); break;
        case MsgType::AddFriend: handleAddFriend(peer, msg); break;
        case MsgType::RespondFriend: handleRespondFriend(peer, msg); break;
        case MsgType::CreateGroup: handleCreateGroup(peer, msg); break;
        default:
            deliver(peer, cc::proto::makeError("bad_type",
                                              "该消息类型不允许由客户端发送"));
    }
    } catch (const std::exception&) {
        auto error = cc::proto::makeError("server_error", "请求未完成，请稍后重试");
        error.msgId = msg.msgId;
        deliver(peer, std::move(error));
    }
}

void Router::handlePasswordReset(Peer& peer, const cc::proto::Message& msg) {
    using cc::proto::MsgType;
    auto reply = [&](bool ok, const std::string& error) {
        deliver(peer, {MsgType::PasswordResetResult, msg.msgId, {{"ok",ok},{"error",error}}});
    };
    if (!usesV2(peer) || !peer.username().empty() || !passwordResetEmail_) {
        reply(false, "请使用支持邮箱重置的新客户端，并先退出登录"); return;
    }
    const auto user = bodyStr(msg.body,"username"), email = auth::normalizedEmail(bodyStr(msg.body,"email"));
    const auto password = bodyStr(msg.body,"password");
    if (!isValidUsername(user) || email.empty() || password.size()<6 || password.size()>1024) {
        reply(false,"请填写有效用户名、绑定邮箱及 6–1024 字节的新密码"); return;
    }
    if (db_.registrationEmail(user) != email) { reply(false,"用户名与已验证邮箱不匹配，或账号尚未绑定邮箱"); return; }
    const auto verified = passwordResetEmail_->consume(user,email,bodyStr(msg.body,"email_code"));
    if (!verified.ok) { reply(false,verified.error); return; }
    const auto salt = cc::crypto::randomBytes(kPbkdf2SaltLen);
    const auto hash = cc::crypto::pbkdf2Sha256(password,salt,kPbkdf2Iterations,kPbkdf2KeyLen);
    db_.updatePasswordAuth(user,hash,salt,kPbkdf2Iterations);
    loginChallenges_.revokeAccount(user);
    if (auto* active = findOnline(user)) {
        active->setUsername("");
        inFlight_.erase(active); directories_.erase(active);
        online_.erase(user);
        deliver(*active,cc::proto::makeError("password_reset", "密码已重置，请重新登录"));
        for (auto& [name, other] : online_) deliver(*other,cc::proto::makePresence(user,false));
    }
    reply(true,"");
}

bool Router::usesV2(const Peer& peer) const {
    auto version = versions_.find(&peer);
    return version != versions_.end() && version->second == 2;
}
void Router::deliver(Peer& peer, cc::proto::Message msg) {
    if (usesV2(peer)) msg.body["version"] = 2;
    peer.deliver(msg);
}

void Router::onDisconnect(Peer& peer) {
    std::lock_guard<std::mutex> lock(routeMutex_);
    loginChallenges_.disconnect(connectionId(peer));
    versions_.erase(&peer);
    directories_.erase(&peer);
    inFlight_.erase(&peer);
    std::erase_if(lastTyping_, [&](const auto& entry) { return entry.first.first == &peer; });
    const std::string user = peer.username();
    const auto online = online_.find(user);
    peer.setUsername("");
    // 同名顶号后旧连接仍可能稍后退出；只撤销当前这条连接拥有的登录。
    if (online != online_.end() && online->second == &peer) {
        online_.erase(online);
        cc::proto::Message p = cc::proto::makePresence(user, false);
        for (auto& [name, p2] : online_) deliver(*p2, p);
    }
}

bool Router::isOnline(const std::string& username) const {
    std::lock_guard<std::mutex> lock(routeMutex_);
    return online_.count(username) != 0;
}

std::vector<std::string> Router::onlineUsers() const {
    std::lock_guard<std::mutex> lock(routeMutex_);
    std::vector<std::string> out;
    out.reserve(online_.size());
    for (const auto& [name, _] : online_) out.push_back(name);
    return out;
}

Peer* Router::findOnline(const std::string& username) const {
    auto it = online_.find(username);
    return it == online_.end() ? nullptr : it->second;
}

bool Router::routeToUser(const std::string& username,
                         const cc::proto::Message& msg) {
    if (Peer* p = findOnline(username)) {
        deliver(*p, msg);
        return true;
    }
    if (auto acc = db_.findAccount(username)) {
        db_.enqueueOffline(acc->id, /*sender*/ "",
                           cc::proto::serialize(msg));
        return false;
    }
    return false;  // 用户不存在：调用方负责错误提示
}

bool Router::requireLogin(Peer& peer, const char* action) {
    if (peer.username().empty() || findOnline(peer.username()) != &peer) {
        deliver(peer, cc::proto::makeError(
            "not_logged_in",
            std::string("需要先登录才能 ") + action));
        return false;
    }
    return true;
}

// ---- 注册 ----

void Router::handleLookupUid(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "查询 UID")) return;
    const auto username = bodyStr(msg.body, "username");
    if (!usesV2(peer) || !isValidUsername(username)) { deliver(peer, cc::proto::makeError("bad_uid_query", "UID 查询账号无效")); return; }
    const auto account = db_.findAccount(username);
    auto response = cc::proto::makeUidResult(username, account ? account->id : 0); response.msgId = msg.msgId;
    deliver(peer, std::move(response));
}
void Router::handleRegister(Peer& peer, const cc::proto::Message& msg) {
    const std::string username = bodyStr(msg.body, "username");
    const std::string password = bodyStr(msg.body, "password");
    if (!isValidUsername(username)) {
        deliver(peer, cc::proto::makeRegisterResult(
            false, "用户名须为 2-32 个字母/数字/下划线/中文", 0));
        return;
    }
    if (password.size() < 6) {
        deliver(peer, cc::proto::makeRegisterResult(false, "口令至少 6 位", 0));
        return;
    }
    if (password.size() > 1024 || bodyStr(msg.body,"chat_mode") != "ordinary-v1") {
        deliver(peer,cc::proto::makeRegisterResult(false,"请升级到普通聊天客户端",0)); return;
    }
    std::string email;
    if (registrationEmail_) {
        email = cc::auth::normalizedEmail(bodyStr(msg.body, "email"));
        if (!usesV2(peer) || email.empty()) {
            deliver(peer, cc::proto::makeRegisterResult(false, "注册需要有效邮箱和邮箱验证码，请升级客户端", 0)); return;
        }
        if (db_.findAccount(username) || db_.emailRegistered(email)) {
            deliver(peer, cc::proto::makeRegisterResult(false, "用户名或邮箱已被注册", 0)); return;
        }
        const auto verified = registrationEmail_->consume(username, email, bodyStr(msg.body, "email_code"));
        if (!verified.ok) { deliver(peer, cc::proto::makeRegisterResult(false, verified.error, 0)); return; }
    }
    const cc::Bytes salt = cc::crypto::randomBytes(kPbkdf2SaltLen);
    const cc::Bytes k = cc::crypto::pbkdf2Sha256(
        password, salt, static_cast<std::uint64_t>(kPbkdf2Iterations),
        kPbkdf2KeyLen);
    auto id = db_.createAccount(username, k, salt, kPbkdf2Iterations, "",
                                "", "", "", email);
    if (!id) {
        deliver(peer, cc::proto::makeRegisterResult(false, "用户名已存在", 0));
        return;
    }
    deliver(peer, cc::proto::makeRegisterResult(true, "", *id));
}

// ---- 登录（挑战-响应）----

void Router::handleLoginRequest(Peer& peer, const cc::proto::Message& msg) {
    if (bodyStr(msg.body,"chat_mode") != "ordinary-v1") {
        deliver(peer,cc::proto::makeLoginResult(false,"请升级到普通聊天客户端")); return;
    }
    const auto id = connectionId(peer);
    // 重新发起请求即作废旧挑战，包括非法账号请求。
    loginChallenges_.disconnect(id);
    const auto nowMs = clock_();
    loginChallenges_.purgeExpired(nowMs);
    const std::string username = bodyStr(msg.body, "username");
    if (!peer.username().empty() && peer.username() != username) {
        deliver(peer, cc::proto::makeLoginResult(false, "切换账号须建立新连接"));
        return;
    }
    auto acc = db_.findAccountByLogin(username);
    if (!acc) {
        deliver(peer, cc::proto::makeLoginResult(false, "用户不存在"));
        return;
    }
    const cc::Bytes nonce = cc::crypto::randomBytes(kLoginNonceLen);
    auto issued = loginChallenges_.issue(
        {id, acc->username, nonce, nowMs + ccfw::server::kLoginChallengeLifetimeMs});
    if (!issued) {
        deliver(peer, cc::proto::makeLoginResult(false, issued.error().message));
        return;
    }
    deliver(peer, cc::proto::makeLoginChallenge(
        cc::base64Encode(nonce), cc::base64Encode(acc->salt),
        acc->iterations));
}

void Router::handleLoginResponse(Peer& peer, const cc::proto::Message& msg) {
    auto pending = loginChallenges_.consume(connectionId(peer), clock_());
    if (!pending) {
        deliver(peer, cc::proto::makeLoginResult(false, pending.error().message));
        return;
    }
    const auto& challenge = pending.value();
    const auto& nonce = challenge.nonce;

    const std::string username = bodyStr(msg.body, "username");
    if (username != challenge.username) {
        deliver(peer, cc::proto::makeLoginResult(false, "响应账号与登录挑战不符"));
        return;
    }
    cc::Bytes proof, echoNonce;
    if (!cc::base64Decode(bodyStr(msg.body, "proof"), proof) ||
        !cc::base64Decode(bodyStr(msg.body, "nonce"), echoNonce) ||
        echoNonce != nonce) {
        deliver(peer, cc::proto::makeLoginResult(false, "登录响应非法"));
        return;
    }
    auto acc = db_.findAccount(username);
    if (!acc) {
        deliver(peer, cc::proto::makeLoginResult(false, "用户不存在"));
        return;
    }
    if (!verifyLoginProof(acc->passwordHash, nonce, proof)) {
        deliver(peer, cc::proto::makeLoginResult(false, "口令错误"));
        return;
    }
    completeLogin(peer,*acc);
}
void Router::handleEmailLogin(Peer& peer,const cc::proto::Message& msg) {
    if(!usesV2(peer) || !peer.username().empty() || !emailLogin_) { deliver(peer,cc::proto::makeLoginResult(false,"当前不能使用邮箱验证码登录"));return; }
    const auto email=auth::normalizedEmail(bodyStr(msg.body,"email"));
    const auto account=db_.findAccountByLogin(email);
    if(email.empty() || !account || db_.registrationEmail(account->username)!=email) { deliver(peer,cc::proto::makeLoginResult(false,"邮箱尚未绑定账号"));return; }
    const auto verified=emailLogin_->consume(account->username,email,bodyStr(msg.body,"email_code"));
    if(!verified.ok) { deliver(peer,cc::proto::makeLoginResult(false,verified.error));return; }
    loginChallenges_.disconnect(connectionId(peer));completeLogin(peer,*account);
}
void Router::completeLogin(Peer& peer,const Storage::Account& account) {
    const auto& username=account.username;
    // 已在线的旧连接踢下线（同名顶号）
    if (Peer* old = findOnline(username)) {
        if (old != &peer) {
            loginChallenges_.disconnect(connectionId(*old));
            inFlight_.erase(old);
            std::erase_if(lastTyping_, [&](const auto& entry) { return entry.first.first == old; });
            old->setUsername("");
            deliver(*old, cc::proto::makeError("kicked", "账号在其他设备登录"));
        }
        online_.erase(username);
    }
    peer.setUsername(username);
    online_[username] = &peer;

    deliver(peer, cc::proto::makeLoginResult(true, "", account.id));
    if (usesV2(peer)) {
        for (const auto& group : db_.groupsForUser(username)) {
            auto update = cc::proto::makeGroupUpdate(group.id, group.name, group.members);
            update.body["revision"] = group.revision; deliver(peer, std::move(update));
        }
    }
    // 广播上线；随后补投离线消息与最新用户列表
    cc::proto::Message p = cc::proto::makePresence(username, true);
    for (auto& [name, p2] : online_) {
        if (p2 != &peer) deliver(*p2, p);
    }
    deliverOfflineQueue(peer);
}

void Router::deliverOfflineQueue(Peer& peer) {
    if (usesV2(peer)) {
        auto queued = db_.deliveries().peek(peer.username(), 64);
        if (!queued) throw std::runtime_error("delivery queue unavailable");
        auto& sent = inFlight_[&peer];
        std::size_t bytes = 0;
        for (const auto& entry : sent) bytes += entry.second;
        for (const auto& message : queued.value()) {
            if (sent.count(message.messageId)) continue;
            if (sent.size() >= 32 || bytes + message.ciphertextJson.size() > 8 * 1024 * 1024) break;
            auto wire = cc::proto::deserialize(message.ciphertextJson);
            if (!wire || !cc::proto::isV2(*wire)) throw std::runtime_error("invalid stored delivery");
            deliver(peer, *wire);
            sent[message.messageId] = message.ciphertextJson.size();
            bytes += message.ciphertextJson.size();
        }
        return;
    }
    auto acc = db_.findAccount(peer.username());
    if (!acc) return;
    for (const auto& m : db_.drainOffline(acc->id)) {
        auto queued = cc::proto::deserialize(m.payloadJson);
        if (queued) deliver(peer, *queued);
    }
}

// ---- 预共享密钥 ----

void Router::handleUploadPrekeys(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "上传预共享密钥")) return;
    auto acc = db_.findAccount(peer.username());
    if (!acc) return;
    const auto keysIt = msg.body.find("keys");
    if (keysIt == msg.body.end() || !keysIt->is_array() || keysIt->empty() ||
        keysIt->size() > 128) {
        deliver(peer, cc::proto::makeUploadPrekeysResult(
            false, 0, "keys 须为 1-128 条的数组"));
        return;
    }
    std::vector<std::pair<std::int64_t, std::string>> rows;
    for (const auto& k : *keysIt) {
        const std::int64_t id = k.value("id", 0);
        const std::string pub = k.value("pub", "");
        if (id <= 0 || !isNonEmptyB64(pub, 64)) {
            deliver(peer, cc::proto::makeUploadPrekeysResult(
                false, 0, "存在非法的密钥条目"));
            return;
        }
        rows.emplace_back(id, pub);
    }
    db_.addOneTimePrekeys(acc->id, rows);
    deliver(peer, cc::proto::makeUploadPrekeysResult(
        true, static_cast<int>(rows.size()), ""));
}

void Router::handleFetchBundle(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "拉取公钥束")) return;
    const std::string target = bodyStr(msg.body, "username");
    auto acc = db_.findAccount(target);
    if (!acc) {
        deliver(peer, cc::proto::makeError("no_user", "用户不存在: " + target));
        return;
    }
    cc::proto::PrekeyBundle bundle;
    bundle.username = acc->username;
    bundle.signPub = acc->signPub;
    bundle.dhPub = acc->dhPub;
    bundle.signedPrekey = acc->signedPrekey;
    bundle.spkSig = acc->spkSig;
    if (auto otp = db_.popOneTimePrekey(acc->id)) {
        bundle.otp = cc::proto::OneTimePrekey{otp->first, otp->second};
    }
    deliver(peer, cc::proto::makeBundle(bundle));
}

// ---- 消息中转 ----

void Router::handleSendMsg(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "发送消息")) return;
    const std::string to = bodyStr(msg.body, "to");
    if (to.empty() || to == peer.username()) {
        deliver(peer, cc::proto::makeError("bad_target", "非法的消息接收方"));
        return;
    }
    if (!db_.findAccount(to)) {
        deliver(peer, cc::proto::makeError("no_user", "用户不存在: " + to));
        return;
    }
    if (usesV2(peer)) {
        const auto kind = bodyStr(msg.body, "kind");
        const auto payload = bodyStr(msg.body, "payload");
        const auto group = msg.body.find("group_id");
        cc::Bytes decoded;
        if (msg.msgId.empty() || msg.msgId.size() > 128 || to.size() > 32 ||
            (kind != "chat" && kind != "file_meta" && kind != "file_chunk" && kind != "file_ack") ||
            payload.empty() || payload.size() > 7 * 1024 * 1024 || !cc::base64Decode(payload, decoded) ||
            (group != msg.body.end() && (!group->is_number_integer() || *group < 0 || *group > INT64_MAX))) {
            deliver(peer, cc::proto::makeError("protocol_error", "消息字段非法"));
            return;
        }
        const auto groupId = cc::proto::bodyInt(msg.body, "group_id");
        if (groupId) {
            ccfw::server::GroupAuthorizer authorizer(db_);
            if (kind != "chat" || !authorizer.authorize(groupId, peer.username(), to)) {
                deliver(peer, cc::proto::makeError("forbidden", "无权向该群成员投递"));
                return;
            }
        }
        // v2 时间由加密内层携带；外层固定 0，密文重发保持完全相同的存储内容。
        const auto received = cc::proto::withV2(cc::proto::makeRecvMsg(
            msg.msgId, peer.username(), to, kind, payload, 0, groupId));
        auto saved = db_.deliveries().enqueue({to, peer.username(), msg.msgId, cc::proto::serialize(received)});
        if (!saved) {
            if (saved.error().code == ccfw::ErrorCode::AlreadyProcessed) {
                auto ack = cc::proto::makeRecipientAck(msg.msgId, peer.username());
                ack.body["from"] = to;
                deliver(peer, std::move(ack));
            } else {
                auto error = cc::proto::makeError("queue_rejected", "无法保存消息，消息标识冲突或队列已满");
                error.msgId = msg.msgId;
                deliver(peer, std::move(error));
            }
            return;
        }
        auto ack = cc::proto::makeMsgAck(msg.msgId, false);
        ack.body["accepted"] = true;
        ack.body["to"] = to;
        deliver(peer, std::move(ack));
        if (Peer* receiver = findOnline(to); receiver && usesV2(*receiver)) deliverOfflineQueue(*receiver);
        return;
    }
    if (Peer* receiver = findOnline(to); receiver && usesV2(*receiver)) {
        deliver(peer, cc::proto::makeError("protocol_error", "对端使用不同协议版本"));
        return;
    }
    const bool online = routeToUser(
        to, cc::proto::makeRecvMsg(msg.msgId, peer.username(), to,
                                   bodyStr(msg.body, "kind"),
                                   bodyStr(msg.body, "payload"),
                                   std::time(nullptr),
                                   cc::proto::bodyInt(msg.body, "group_id")));
    deliver(peer, cc::proto::makeMsgAck(msg.msgId, online));
}

void Router::handleSocial(Peer& peer, const cc::proto::Message& msg) {
    if(!requireLogin(peer,"个人资料")||!usesV2(peer))return;
    cc::proto::Message response{cc::proto::MsgType::SocialResult,msg.msgId,{{"ok",false},{"op",bodyStr(msg.body,"op")},{"username",bodyStr(msg.body,"username",peer.username())}}};
    response.body["view_id"]=bodyStr(msg.body,"view_id");
    try { const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();response.body=db_.socialRequest(peer.username(),msg.body,now); }
    catch(const std::exception& error) {
        const std::string reason=error.what();const std::vector<std::string> known={"forbidden","not_found","invalid_user","invalid_text","empty_text","text_too_long","invalid_avatar","invalid_cursor","quota_reached","conflict","invalid_request"};
        response.body["error"]=std::find(known.begin(),known.end(),reason)!=known.end()?reason:"invalid_request";
    }
    deliver(peer,std::move(response));
}

void Router::handleFileCache(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer,"文件缓存") || !usesV2(peer)) return;
    using cc::proto::MsgType;
    const auto id = bodyStr(msg.body,"file_id"),op = bodyStr(msg.body,"op");
    cc::proto::Message response{MsgType::FileCacheResult,msg.msgId,{{"ok",false},{"op",op},{"file_id",id}}};
    try {
        cc::Bytes raw; if (id.size()>32 || !cc::base64Decode(id,raw) || raw.size()!=16) throw std::runtime_error("bad cache id");
        if (op=="begin") {
            const auto to=bodyStr(msg.body,"to"); if(!db_.findAccount(to)) throw std::runtime_error("bad recipient");
            const auto size=msg.body.at("size").get<std::uint64_t>(); const auto chunks=msg.body.at("chunks").get<std::uint32_t>();
            auto status=db_.fileCache().begin(peer.username(),id,to,size,chunks);
            response.body.update(status);response.body["ok"]=true;
        } else if(op=="put") {
            const auto seq=msg.body.at("seq").get<std::uint32_t>(); const auto data=bodyStr(msg.body,"data");
            if(data.size()>180*1024 || !cc::base64Decode(data,raw)) throw std::runtime_error("bad block");
            response.body["next"]=db_.fileCache().put(peer.username(),id,seq,raw);response.body["ok"]=true;
        } else if(op=="cancel") {
            db_.fileCache().cancel(peer.username(),id);response.body["ok"]=true;
        } else if(op=="get") {
            const auto owner=bodyStr(msg.body,"owner"); const auto start=msg.body.at("seq").get<std::uint32_t>();
            const auto count=msg.body.value("count",1u);if(count<1 || count>16 || start>16384-count)throw std::runtime_error("bad window");
            response.body["owner"]=owner;
            for(unsigned i=0;i<count;++i) {
                const auto data=db_.fileCache().get(peer.username(),owner,id,start+i);
                response.body["ok"]=true;response.body["seq"]=start+i;response.body["available"]=bool(data);
                if(data)response.body["data"]=cc::base64Encode(*data);else response.body.erase("data");
                deliver(peer,response);if(!data)break;
            }
            return;
        } else throw std::runtime_error("bad operation");
    } catch(const std::exception&) { response.body["ok"]=false;response.body.erase("data");response.body["error"]="cache_unavailable"; }
    deliver(peer,std::move(response));
}

void Router::handleFileAck(Peer& peer, const cc::proto::Message& msg) {
    if (usesV2(peer)) {
        deliver(peer, cc::proto::makeError("protocol_error", "文件确认必须经过会话加密"));
        return;
    }
    if (!requireLogin(peer, "发送文件确认")) return;
    const std::string to = bodyStr(msg.body, "to");
    if (to.empty()) {
        deliver(peer, cc::proto::makeError("bad_target", "file_ack 缺少 to"));
        return;
    }
    // 服务器透传给原发送方：还原 msg_id 与 seq
    Peer* p = findOnline(to);
    if (p) {
        deliver(*p, cc::proto::makeFileAck(msg.msgId,
                                          cc::proto::bodyInt(msg.body, "seq")));
    }
    // 不在线时静默丢弃：文件续传由发送方超时重发
}

void Router::handleRecipientAck(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "签收消息")) return;
    if (msg.msgId.empty() || msg.msgId.size() > 128 || bodyStr(msg.body, "to").empty()) {
        deliver(peer, cc::proto::makeError("protocol_error", "签收字段非法"));
        return;
    }
    auto message = db_.deliveries().find(peer.username(), msg.msgId);
    if (!message) throw std::runtime_error("delivery lookup failed");
    if (!message.value()) return; // 重复/未知 ACK 不改变任何记录。
    if (message.value()->sender != bodyStr(msg.body, "to")) {
        deliver(peer, cc::proto::makeError("forbidden", "签收目标不匹配"));
        return;
    }
    if (!db_.deliveries().acknowledge(peer.username(), msg.msgId)) throw std::runtime_error("acknowledgement failed");
    inFlight_[&peer].erase(msg.msgId);
    if (Peer* sender = findOnline(message.value()->sender); sender && usesV2(*sender)) {
        auto ack = cc::proto::makeRecipientAck(msg.msgId, message.value()->sender);
        ack.body["from"] = peer.username();
        deliver(*sender, std::move(ack));
    }
    deliverOfflineQueue(peer);
}

void Router::handleTyping(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "发送输入提示")) return;
    const std::string to = bodyStr(msg.body, "to");
    if (to.empty() || to.size() > 32 || to == peer.username()) return;
    if (usesV2(peer)) {
        const auto now = clock_(); const auto key = std::make_pair(&peer, to); const auto last = lastTyping_.find(key);
        if (last != lastTyping_.end() && now - last->second < 1000) return;
        std::erase_if(lastTyping_, [&](const auto& entry) { return now - entry.second >= 3000; });
        if (lastTyping_.size() >= 4096) return;
        lastTyping_[key] = now;
    }
    if (Peer* p = findOnline(to)) deliver(*p, cc::proto::makeTypingFrom(peer.username()));
}

void Router::handleListUsers(Peer& peer) {
    if (!requireLogin(peer, "获取用户列表")) return;
    std::vector<std::pair<std::string, bool>> users;
    for (const std::string& name : db_.allUsernames()) {
        // onMessage 已持有路由锁，不再次调用会加锁的公开查询。
        users.emplace_back(name, findOnline(name) != nullptr);
    }
    deliver(peer, cc::proto::makeUserList(users));
}

void Router::deliverDirectory(Peer& peer) {
    if (!directories_.count(&peer)) return; // Old clients do not understand contact_list.
    std::vector<std::pair<std::string, bool>> users;
    for (const auto& name : db_.friendsForUser(peer.username())) users.emplace_back(name, findOnline(name) != nullptr);
    auto response = cc::proto::makeContactList(users);
    response.body["incoming"] = db_.friendRequests(peer.username(), true);
    response.body["outgoing"] = db_.friendRequests(peer.username(), false);
    deliver(peer, std::move(response));
}

void Router::handleListContacts(Peer& peer) {
    if (!requireLogin(peer, "获取联系人")) return;
    directories_[&peer] = true;
    deliverDirectory(peer);
}

void Router::handleSearchUsers(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "查询陌生人")) return;
    const auto query = bodyStr(msg.body, "query");
    const auto after = bodyStr(msg.body, "after");
    if (!msg.body.contains("query") || !msg.body["query"].is_string() || query.size() > 32 || after.size() > 32 ||
        (msg.body.contains("after") && !msg.body["after"].is_string())) {
        deliver(peer, cc::proto::makeError("invalid_argument", "查询格式无效")); return;
    }
    std::vector<std::pair<std::string, bool>> users;
    for (const auto& name : db_.searchUsernames(peer.username(), query, after)) users.emplace_back(name, findOnline(name) != nullptr);
    const bool more = users.size() > 50; if (more) users.resize(50);
    auto response = cc::proto::makeSearchResult(users); response.msgId = msg.msgId;
    response.body["query"] = query; response.body["after"] = after;
    response.body["next"] = more ? users.back().first : "";
    deliver(peer, std::move(response));
}

void Router::handleAddFriend(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "添加好友")) return;
    const auto username = bodyStr(msg.body, "username");
    if (username.empty() || username.size() > 32 || username == peer.username()) {
        deliver(peer, cc::proto::makeFriendResult(false, "用户名无效", username)); return;
    }
    const auto state = db_.requestFriend(peer.username(), username);
    if (state.empty()) {
        deliver(peer, cc::proto::makeFriendResult(false, "用户不存在或待处理申请过多", username)); return;
    }
    auto response = cc::proto::makeFriendResult(true, "", username); response.body["state"] = state;
    deliver(peer, std::move(response));
    directories_[&peer] = true;
    deliverDirectory(peer);
    if (auto* other = findOnline(username)) deliverDirectory(*other);
}

void Router::handleRespondFriend(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "处理好友申请")) return;
    const auto username = bodyStr(msg.body, "username");
    if (username.empty() || username.size() > 32 || !msg.body.contains("accept") || !msg.body["accept"].is_boolean()) {
        deliver(peer, cc::proto::makeFriendResult(false, "申请格式无效", username)); return;
    }
    const bool accept = cc::proto::bodyBool(msg.body, "accept");
    if (!db_.respondFriend(peer.username(), username, accept)) {
        deliver(peer, cc::proto::makeFriendResult(false, "该好友申请不存在或已处理", username)); return;
    }
    auto response = cc::proto::makeFriendResult(true, "", username); response.body["state"] = accept ? "accepted" : "declined";
    deliver(peer, std::move(response));
    directories_[&peer] = true; deliverDirectory(peer);
    if (auto* other = findOnline(username)) deliverDirectory(*other);
}

void Router::handleCreateGroup(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "创建群组")) return;
    const std::string name = bodyStr(msg.body, "name");
    if (usesV2(peer)) {
        const auto list = msg.body.find("members");
        if (name.size() > 128 || list == msg.body.end() || !list->is_array() || list->size() > 64 ||
            std::any_of(list->begin(), list->end(), [](const auto& member) { return !member.is_string() || member.template get<std::string>().empty() || member.template get<std::string>().size() > 32; })) {
            deliver(peer, cc::proto::makeError("protocol_error", "群名或成员字段非法")); return;
        }
    }
    std::vector<std::string> members;
    if (auto it = msg.body.find("members"); it != msg.body.end() &&
                                             it->is_array()) {
        for (const auto& m : *it) {
            if (m.is_string()) members.push_back(m.get<std::string>());
        }
    }
    // 创建者自动加入；加入后成员须 2-64 人
    auto unique = members; std::sort(unique.begin(), unique.end());
    if (name.empty() || members.size() > 64 || std::adjacent_find(unique.begin(), unique.end()) != unique.end()) {
        deliver(peer, cc::proto::makeGroupResult(
            false, "群名不能为空且成员须 2-64 人", 0, "", {}));
        return;
    }
    if (std::find(members.begin(), members.end(), peer.username()) ==
        members.end()) {
        members.push_back(peer.username());
    }
    if (members.size() < 2 || members.size() > 64) {
        deliver(peer, cc::proto::makeGroupResult(
            false, "群名不能为空且成员须 2-64 人", 0, "", {}));
        return;
    }
    for (const auto& m : members) {
        if (!db_.findAccount(m)) {
            deliver(peer, cc::proto::makeGroupResult(
                false, "成员不存在: " + m, 0, "", {}));
            return;
        }
    }
    auto gid = db_.createGroup(name, members);
    if (!gid) {
        deliver(peer, cc::proto::makeGroupResult(false, "创建失败", 0, "", {}));
        return;
    }
    deliver(peer, cc::proto::makeGroupResult(true, "", *gid, name, members));
    // 通知其他在线成员
    for (const auto& m : members) {
        if (m != peer.username()) {
            if (Peer* p = findOnline(m)) {
                deliver(*p, cc::proto::makeGroupUpdate(*gid, name, members));
            }
        }
    }
}

}  // namespace cc::server
