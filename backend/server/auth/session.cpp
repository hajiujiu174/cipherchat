#include "server/router/router.hpp"
#include "server/router/validation.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include "common/crypto/crypto.hpp"
#include "common/auth/password.hpp"
#include "common/auth/email.hpp"
#include <ctime>
#include <cstdlib>
#include <cstring>

namespace cc::server {

using cc::proto::bodyStr;
using auth::kPbkdf2Iterations;
using auth::kPbkdf2KeyLen;
using auth::kPbkdf2SaltLen;

// 核对绑定邮箱验证码，更新密码验证信息并撤销原有在线会话。
void Router::handlePasswordReset(Peer& peer, const cc::proto::Message& msg) {
    using cc::proto::MsgType;
    // 回复账号认证。
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
    if (auto* active = findOnline(user)) {
        active->setUsername("");
        inFlight_.erase(active); directories_.erase(active);
        online_.erase(user);
        deliver(*active,cc::proto::makeError("password_reset", "密码已重置，请重新登录"));
        for (auto& [name, other] : online_) deliver(*other,cc::proto::makePresence(user,false));
    }
    reply(true,"");
}

// 校验注册请求与邮箱验证码，通过后在服务端创建账号。
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

// 读取 TLS 连接提交的密码，用账号原有盐值和迭代次数重新计算哈希。
void Router::handleLoginRequest(Peer& peer, const cc::proto::Message& msg) {
    if (bodyStr(msg.body, "chat_mode") != "ordinary-v1" || !msg.body.contains("password")) {
        deliver(peer, cc::proto::makeLoginResult(false, "请升级到支持密码登录的 NEXTALK 客户端"));
        return;
    }

    const auto& passwordField = msg.body["password"];
    if (!passwordField.is_string()) {
        deliver(peer, cc::proto::makeLoginResult(false, "账号或密码错误"));
        return;
    }
    const auto& password = passwordField.get_ref<const std::string&>();
    if (password.size() < 6 || password.size() > 1024) {
        deliver(peer, cc::proto::makeLoginResult(false, "账号或密码错误"));
        return;
    }

    const auto account = db_.findAccountByLogin(bodyStr(msg.body, "username"));
    if (!account || account->salt.size() != kPbkdf2SaltLen ||
        account->passwordHash.size() != kPbkdf2KeyLen ||
        account->iterations < 10'000 || account->iterations > 1'000'000) {
        deliver(peer, cc::proto::makeLoginResult(false, "账号或密码错误"));
        return;
    }
    if (!peer.username().empty() && peer.username() != account->username) {
        deliver(peer, cc::proto::makeLoginResult(false, "切换账号须建立新连接"));
        return;
    }

    const auto hash = cc::crypto::pbkdf2Sha256(
        password, account->salt, account->iterations, kPbkdf2KeyLen);
    if (!cc::crypto::constantTimeEqual(hash, account->passwordHash)) {
        deliver(peer, cc::proto::makeLoginResult(false, "账号或密码错误"));
        return;
    }
    completeLogin(peer, *account);
}

// 核对绑定邮箱与登录验证码，通过后将当前连接关联到原账号。
void Router::handleEmailLogin(Peer& peer,const cc::proto::Message& msg) {
    if(!usesV2(peer) || !peer.username().empty() || !emailLogin_) { deliver(peer,cc::proto::makeLoginResult(false,"当前不能使用邮箱验证码登录"));return; }
    const auto email=auth::normalizedEmail(bodyStr(msg.body,"email"));
    const auto account=db_.findAccountByLogin(email);
    if(email.empty() || !account || db_.registrationEmail(account->username)!=email) { deliver(peer,cc::proto::makeLoginResult(false,"邮箱尚未绑定账号"));return; }
    const auto verified=emailLogin_->consume(account->username,email,bodyStr(msg.body,"email_code"));
    if(!verified.ok) { deliver(peer,cc::proto::makeLoginResult(false,verified.error));return; }
    completeLogin(peer,*account);
}

// 把认证成功的账号绑定到连接，发出初始状态并补投未签收消息。
void Router::completeLogin(Peer& peer,const Storage::Account& account) {
    const auto& username=account.username;
    // 已在线的旧连接踢下线（同名顶号）
    if (Peer* old = findOnline(username)) {
        if (old != &peer) {
            inFlight_.erase(old);
            std::erase_if(lastTyping_, [&](const auto& entry) { return entry.first.first == old; });
            lastGameRelay_.erase(old);
            gamePeers_.erase(old);
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

} // namespace cc::server
