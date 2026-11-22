#include "server/router/router.hpp"
#include "server/router/validation.hpp"

#include "common/crypto/crypto.hpp"
#include "common/auth/email.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <utility>
#include <cstdlib>
#include <cstring>

namespace cc::server {

namespace {

using cc::proto::bodyStr;

}  // namespace

// 构造服务端协议路由器，初始化后续操作需要的状态或依赖。
Router::Router(Storage& db, Clock clock)
    : db_(db), clock_(clock ? std::move(clock) : Clock{[] {
          return std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
      }}) {}

// 检查应用协议版本，再把已解析的请求分发到对应业务处理函数。
void Router::onMessage(Peer& peer, const cc::proto::Message& msg) {
    std::unique_lock<std::mutex> lock(routeMutex_);
    using cc::proto::MsgType;
    if (msg.type == MsgType::Hello) {
        const bool ok = cc::proto::isV2(msg) && peer.username().empty() &&
            versions_.find(&peer) == versions_.end();
        if (ok) versions_[&peer] = 2;
        else if (!versions_.count(&peer)) versions_[&peer] = 0;
        auto response = cc::proto::makeHelloResult(ok); response.body["uid_lookup"] = ok; response.body["chat_mode"] = "ordinary-v1";
        response.body["password_login"] = ok;
        response.body["file_cache"] = ok && fileCacheEnabled_;
        response.body["profile_moments"] = ok;
        response.body["game_relay"] = ok && cc::proto::bodyBool(msg.body,"game_relay");
        if(ok)gamePeers_[&peer]=response.body["game_relay"].get<bool>();
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
    // 收到业务消息后冻结 v1，不允许在连接中途升级协议。
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
        case MsgType::GameRelay: handleGameRelay(peer,msg); break;
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

// 查询该连接是否协商为协议版本 2。
bool Router::usesV2(const Peer& peer) const {
    auto version = versions_.find(&peer);
    return version != versions_.end() && version->second == 2;
}
// 投递服务端协议路由器。
void Router::deliver(Peer& peer, cc::proto::Message msg) {
    if (usesV2(peer)) msg.body["version"] = 2;
    peer.deliver(msg);
}

// 清理连接的在线和在途状态，并向其他连接发布离线事件。
void Router::onDisconnect(Peer& peer) {
    std::lock_guard<std::mutex> lock(routeMutex_);
    versions_.erase(&peer);
    directories_.erase(&peer);
    inFlight_.erase(&peer);
    std::erase_if(lastTyping_, [&](const auto& entry) { return entry.first.first == &peer; });
    lastGameRelay_.erase(&peer);
    gamePeers_.erase(&peer);
    const std::string user = peer.username();
    for(auto it=calls_.begin();it!=calls_.end();) {
        if(it->second.caller==user||it->second.callee==user){const auto other=it->second.caller==user?it->second.callee:it->second.caller;if(auto* target=findOnline(other))deliver(*target,cc::proto::withV2({cc::proto::MsgType::SocialResult,"",{{"op","call"},{"ok",true},{"action","end"},{"call_id",it->first},{"reason","disconnected"}}}));it=calls_.erase(it);}else ++it;
    }
    const auto online = online_.find(user);
    peer.setUsername("");
    // 同名顶号后旧连接仍可能稍后退出；只撤销当前这条连接拥有的登录。
    if (online != online_.end() && online->second == &peer) {
        online_.erase(online);
        cc::proto::Message p = cc::proto::makePresence(user, false);
        for (auto& [name, p2] : online_) deliver(*p2, p);
    }
}

// 判断在线条件是否成立。
bool Router::isOnline(const std::string& username) const {
    std::lock_guard<std::mutex> lock(routeMutex_);
    return online_.count(username) != 0;
}

// 返回服务端协议路由器使用的在线用户。
std::vector<std::string> Router::onlineUsers() const {
    std::lock_guard<std::mutex> lock(routeMutex_);
    std::vector<std::string> out;
    out.reserve(online_.size());
    for (const auto& [name, _] : online_) out.push_back(name);
    return out;
}

// 查找在线。
Peer* Router::findOnline(const std::string& username) const {
    auto it = online_.find(username);
    return it == online_.end() ? nullptr : it->second;
}

// 返回服务端协议路由器使用的路由To用户。
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

// 要求登录。
bool Router::requireLogin(Peer& peer, const char* action) {
    if (peer.username().empty() || findOnline(peer.username()) != &peer) {
        deliver(peer, cc::proto::makeError(
            "not_logged_in",
            std::string("需要先登录才能 ") + action));
        return false;
    }
    return true;
}

// Online-only game packets never enter Storage, offline queues, or receipt tables.

#include "server/calls/relay.inc"
}  // namespace cc::server
