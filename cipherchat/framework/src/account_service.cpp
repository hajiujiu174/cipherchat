#include "ccfw/account_service.hpp"
#include "common/auth/login_proof.hpp"
#include "common/auth/email.hpp"
#include <openssl/crypto.h>
#include <algorithm>
namespace ccfw {
namespace {
Result<> accountError(ErrorCode code, const char* message) { return Result<>::failure({code, "M3-02", message}); }
bool validCredentials(const std::string& user, const std::string& password) {
    return user.size() >= 2 && user.size() <= 32 && password.size() >= 6 && password.size() <= 1024;
}
}
AccountService::AccountService(ITransport& net, ILocalStore& store, Clock clock)
    : net_(net), store_(store), clock_(clock ? std::move(clock) : Clock{[] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }}) {}
AccountService::~AccountService() { clearPassword(); }
void AccountService::clearPassword() noexcept {
    if (!password_.empty()) OPENSSL_cleanse(password_.data(), password_.size());
    password_.clear();
}
void AccountService::beginWait(AccountPhase phase) { phase_ = phase; deadlineMs_ = clock_() + 10000; }
Result<> AccountService::checkTimeout() {
    if (deadlineMs_ && clock_() >= deadlineMs_) {
        clearPassword(); deadlineMs_ = 0; responseSent_ = false;
        // 关闭连接避免迟到响应被下一次认证请求误接收。
        net_.close(); phase_ = AccountPhase::Disconnected;
        return accountError(ErrorCode::Timeout, "服务器认证响应超时，请重新连接");
    }
    return Result<>::success({});
}
Result<> AccountService::negotiateProtocol() {
    if (phase_ != AccountPhase::TransportConnected) return accountError(ErrorCode::InvalidState, "请先建立安全连接");
    auto hello = cc::proto::makeHello(); hello.body["chat_mode"] = "ordinary-v1";
    if(!loginAlias_.empty())hello.body["login_identifier"]=loginAlias_;
    auto sent = net_.send(hello);
    if (!sent) return sent;
    beginWait(AccountPhase::Negotiating);
    return Result<>::success({});
}
Result<> AccountService::registerAccount(const std::string& user, const std::string& password, const std::string& email, const std::string& emailCode) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState, "当前不可注册，请等待连接或当前请求完成");
    if (!validCredentials(user, password)) return accountError(ErrorCode::InvalidArgument, "用户名或口令长度非法");
    if ((!email.empty() || !emailCode.empty()) && !registrationEmailSupported_)
        return accountError(ErrorCode::NotImplemented, "当前服务器尚未升级邮箱注册，请联系管理员");
    if (registrationEmailSupported_ && (cc::auth::normalizedEmail(email).empty() || emailCode.size() != 6))
        return accountError(ErrorCode::InvalidArgument, "请填写注册邮箱和六位验证码");
    auto loaded = store_.load();
    if (!loaded) return Result<>::failure(loaded.error());
    auto next = loaded.value();
    if (next.account.username != user) return accountError(ErrorCode::InvalidArgument, "账号与本地存储不匹配");
    auto request = cc::proto::withV2({cc::proto::MsgType::Register, "", {{"username",user},{"password",password},{"chat_mode","ordinary-v1"}}});
    if (registrationEmailSupported_) { request.body["email"] = email; request.body["email_code"] = emailCode; }
    auto sent=net_.send(request); if (!sent) return sent;
    beginWait(AccountPhase::Registering); return Result<>::success({});
}

Result<> AccountService::requestRegistrationCode(const std::string& username, const std::string& email, bool passwordReset, bool emailLogin) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState, "请等待安全连接或当前请求完成");
    if (emailLogin ? !emailLoginSupported_ : passwordReset ? !passwordResetSupported_ : !registrationEmailSupported_) return accountError(ErrorCode::NotImplemented, "当前服务器尚未支持此邮箱验证功能，请联系管理员");
    if (cc::auth::normalizedEmail(email).empty()) return accountError(ErrorCode::InvalidArgument, "请输入有效的邮箱地址");
    auto sent = net_.send(cc::proto::withV2({cc::proto::MsgType::RegistrationCode, "", {{"username", username}, {"email", email}, {"purpose",emailLogin ? "email_login" : passwordReset ? "password_reset" : "registration"}}}));
    if (sent) { codePending_ = true; codeResult_.reset(); deadlineMs_ = clock_() + 30000; }
    return sent;
}
std::optional<cc::proto::Message> AccountService::takeRegistrationCodeResult() {
    auto result = std::move(codeResult_); codeResult_.reset(); return result;
}
Result<> AccountService::resetPassword(const std::string& user, const std::string& email, const std::string& code, const std::string& password) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState,"请等待安全连接或当前请求完成");
    if (!passwordResetSupported_) return accountError(ErrorCode::NotImplemented,"服务器暂不支持邮箱重置密码");
    if (!validCredentials(user,password) || cc::auth::normalizedEmail(email).empty() || code.size()!=6)
        return accountError(ErrorCode::InvalidArgument,"请填写用户名、绑定邮箱、六位验证码及至少六位的新密码");
    auto sent=net_.send(cc::proto::withV2({cc::proto::MsgType::PasswordReset,"",{{"username",user},{"email",email},{"email_code",code},{"password",password}}}));
    if (sent) { resetPending_=true; codeResult_.reset(); deadlineMs_=clock_()+30000; }
    return sent;
}
Result<> AccountService::login(const std::string& user, const std::string& password) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState, "当前不可登录，请等待连接或当前请求完成");
    if (password.size()<6 || password.size()>1024 || (user!=loginAlias_ && !validCredentials(user,password))) return accountError(ErrorCode::InvalidArgument, "用户名、邮箱或密码格式无效");
    auto snapshot = store_.load();
    if (!snapshot) return Result<>::failure(snapshot.error());
    if (snapshot.value().account.username != user && user!=loginAlias_) return accountError(ErrorCode::InvalidArgument, "账号与本地存储不匹配");
    clearPassword(); password_ = password; responseSent_ = false;
    auto request = cc::proto::withV2(cc::proto::makeLoginRequest(user)); request.body["chat_mode"]="ordinary-v1";
    auto sent = net_.send(request);
    if (!sent) { clearPassword(); return sent; }
    beginWait(AccountPhase::ChallengePending);
    return Result<>::success({});
}
Result<> AccountService::loginWithEmailCode(const std::string& email,const std::string& code) {
    if(phase_!=AccountPhase::Connected || codePending_ || resetPending_ || !emailLoginSupported_)return accountError(ErrorCode::InvalidState,"服务器暂不支持邮箱验证码登录");
    if(cc::auth::normalizedEmail(email).empty() || code.size()!=6 || !std::all_of(code.begin(),code.end(),[](char c){return c>='0'&&c<='9';}))return accountError(ErrorCode::InvalidArgument,"请填写邮箱和六位验证码");
    auto sent=net_.send(cc::proto::withV2({cc::proto::MsgType::EmailLogin,"",{{"email",email},{"email_code",code}}}));
    if(!sent)return sent;clearPassword();responseSent_=true;beginWait(AccountPhase::ChallengePending);return Result<>::success({});
}
Result<> AccountService::uploadPrekeys(std::size_t) { return accountError(ErrorCode::NotImplemented,"普通聊天不使用预共享密钥"); }
Result<> AccountService::handle(const cc::proto::Message& message) {
    using namespace cc::proto;
    if (auto timeout = checkTimeout(); !timeout) return timeout;
    auto invalid = [this] {
        clearPassword(); deadlineMs_ = 0; responseSent_ = false;
        net_.close(); phase_ = AccountPhase::Disconnected;
        return accountError(ErrorCode::ProtocolError, "服务器认证响应格式或顺序非法");
    };
    if (!isV2(message)) return invalid();
    if (message.type == MsgType::HelloResult && phase_ == AccountPhase::Negotiating) {
        if (!bodyBool(message.body, "ok")) return invalid();
        if (bodyStr(message.body,"chat_mode") != "ordinary-v1") {
            net_.close(); phase_=AccountPhase::Disconnected; deadlineMs_=0;
            return accountError(ErrorCode::ProtocolError,"服务器尚未升级普通聊天版本，请联系管理员升级服务器");
        }
        uidLookupSupported_ = bodyBool(message.body, "uid_lookup");
        emailLoginSupported_=bodyBool(message.body,"email_code_login");
        if(bindAccount_) {
            const auto canonical=bodyStr(message.body,"login_username");
            if(canonical.empty() || canonical.size()>32) { net_.close();phase_=AccountPhase::Disconnected;deadlineMs_=0;return accountError(ErrorCode::AuthenticationFailed,"用户名或邮箱不存在"); }
            auto bound=bindAccount_(canonical);if(!bound){net_.close();phase_=AccountPhase::Disconnected;deadlineMs_=0;return bound;}
        }
        registrationEmailSupported_ = bodyBool(message.body, "registration_email");
        passwordResetSupported_ = bodyBool(message.body,"password_reset_email");
        phase_ = AccountPhase::Connected; deadlineMs_ = 0;
        return Result<>::success({});
    }
    if (message.type == MsgType::RegistrationCodeResult && codePending_ && phase_ == AccountPhase::Connected) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean()) return invalid();
        codePending_ = false; deadlineMs_ = 0; codeResult_ = message;
        return Result<>::success({});
    }
    if (message.type == MsgType::PasswordResetResult && resetPending_ && phase_ == AccountPhase::Connected) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean()) return invalid();
        resetPending_=false; deadlineMs_=0; codeResult_=message;
        return Result<>::success({});
    }
    if (message.type == MsgType::RegisterResult && phase_ == AccountPhase::Registering) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean()) return invalid();
        if (bodyBool(message.body,"ok") && bodyInt(message.body,"user_id") > 0) userId_ = bodyInt(message.body,"user_id");
        phase_ = AccountPhase::Connected; deadlineMs_ = 0;
        return bodyBool(message.body, "ok") ? Result<>::success({}) : Result<>::failure({ErrorCode::Conflict, "M3-02", bodyStr(message.body, "error", "注册未成功")});
    }
    if (message.type == MsgType::LoginChallenge && phase_ == AccountPhase::ChallengePending && !responseSent_) {
        cc::Bytes nonce, salt;
        const auto iterations = bodyInt(message.body, "iterations");
        if (!cc::base64Decode(bodyStr(message.body, "nonce"), nonce) || nonce.size() != 16 ||
            !cc::base64Decode(bodyStr(message.body, "salt"), salt) || salt.size() != 16 || iterations < 10000 || iterations > 1000000)
            return invalid();
        auto snapshot = store_.load();
        if (!snapshot) { clearPassword(); return invalid(); }
        try {
            auto key = cc::crypto::pbkdf2Sha256(password_, salt, iterations, 32);
            clearPassword();
            const auto proof = cc::auth::deriveLoginProof(key, nonce);
            OPENSSL_cleanse(key.data(), key.size());
            auto sent = net_.send(withV2(makeLoginResponse(snapshot.value().account.username,
                cc::base64Encode(proof), cc::base64Encode(nonce), "")));
            if (!sent) { connectionChanged(false); net_.close(); return sent; }
            responseSent_ = true;
            return Result<>::success({});
        } catch (const std::exception&) { return invalid(); }
    }
    if (message.type == MsgType::LoginResult && phase_ == AccountPhase::ChallengePending) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean() || (bodyBool(message.body, "ok") && !responseSent_)) return invalid();
        clearPassword(); deadlineMs_ = 0; responseSent_ = false;
        const bool ok = bodyBool(message.body, "ok"); phase_ = ok ? AccountPhase::Online : AccountPhase::Connected;
        if (ok && message.body.contains("user_id")) {
            if (!message.body["user_id"].is_number_integer() || bodyInt(message.body,"user_id") <= 0) { phase_ = AccountPhase::Connected; return invalid(); }
            userId_ = bodyInt(message.body,"user_id");
        }
        return ok ? Result<>::success({}) : Result<>::failure({ErrorCode::AuthenticationFailed,"ordinary-chat","登录失败："+bodyStr(message.body,"error","请检查账号和密码")});
    }
    if (message.type == MsgType::UploadPrekeysResult && phase_ == AccountPhase::Online) {
        return bodyBool(message.body, "ok") ? Result<>::success({}) : accountError(ErrorCode::ProtocolError, "预共享密钥上传失败");
    }
    return invalid();
}
void AccountService::connectionChanged(bool connected) noexcept {
    clearPassword(); responseSent_ = false; deadlineMs_ = 0;
    userId_ = 0; uidLookupSupported_ = false; emailLoginSupported_=false; registrationEmailSupported_ = false; codePending_ = false; codeResult_.reset();
    passwordResetSupported_=false; resetPending_=false;
    phase_ = connected ? AccountPhase::TransportConnected : AccountPhase::Disconnected;
}
}
