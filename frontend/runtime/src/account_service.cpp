#include "ccfw/account_service.hpp"
#include "common/auth/email.hpp"
#include <openssl/crypto.h>
#include <algorithm>
namespace ccfw {
namespace {
// 返回account_service 模块使用的账号错误。
Result<> accountError(ErrorCode code, const char* message) { return Result<>::failure({code, "M3-02", message}); }
// 判断给定的Credentials数据是否有效。
bool validCredentials(const std::string& user, const std::string& password) {
    return user.size() >= 2 && user.size() <= 32 && password.size() >= 6 && password.size() <= 1024;
}
}
// 构造客户端账号服务，初始化后续操作需要的状态或依赖。
AccountService::AccountService(ITransport& net, ILocalStore& store, Clock clock)
    : net_(net), store_(store), clock_(clock ? std::move(clock) : Clock{[] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }}) {}
// 开始Wait。
void AccountService::beginWait(AccountPhase phase) { phase_ = phase; deadlineMs_ = clock_() + 10000; }
// 检查超时，交给调用方区分有效结果与错误。
Result<> AccountService::checkTimeout() {
    if (deadlineMs_ && clock_() >= deadlineMs_) {
        deadlineMs_ = 0;
        // 关闭连接避免迟到响应被下一次认证请求误接收。
        net_.close(); phase_ = AccountPhase::Disconnected;
        return accountError(ErrorCode::Timeout, "服务器认证响应超时，请重新连接");
    }
    return Result<>::success({});
}
// 发起应用层 Hello 协商以获得服务端协议版本和支持的功能。
Result<> AccountService::negotiateProtocol() {
    if (phase_ != AccountPhase::TransportConnected) return accountError(ErrorCode::InvalidState, "请先建立安全连接");
    auto hello = cc::proto::makeHello(); hello.body["chat_mode"] = "ordinary-v1";
    if(!loginAlias_.empty())hello.body["login_identifier"]=loginAlias_;
    auto sent = net_.send(hello);
    if (!sent) return sent;
    beginWait(AccountPhase::Negotiating);
    return Result<>::success({});
}
// 校验注册凭据与邮箱验证码后，通过当前连接提交注册申请。
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

// 请求注册验证码。
Result<> AccountService::requestRegistrationCode(const std::string& username, const std::string& email, bool passwordReset, bool emailLogin) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState, "请等待安全连接或当前请求完成");
    if (emailLogin ? !emailLoginSupported_ : passwordReset ? !passwordResetSupported_ : !registrationEmailSupported_) return accountError(ErrorCode::NotImplemented, "当前服务器尚未支持此邮箱验证功能，请联系管理员");
    if (cc::auth::normalizedEmail(email).empty()) return accountError(ErrorCode::InvalidArgument, "请输入有效的邮箱地址");
    auto sent = net_.send(cc::proto::withV2({cc::proto::MsgType::RegistrationCode, "", {{"username", username}, {"email", email}, {"purpose",emailLogin ? "email_login" : passwordReset ? "password_reset" : "registration"}}}));
    if (sent) { codePending_ = true; codeResult_.reset(); deadlineMs_ = clock_() + 30000; }
    return sent;
}
// 取出注册验证码结果。
std::optional<cc::proto::Message> AccountService::takeRegistrationCodeResult() {
    auto result = std::move(codeResult_); codeResult_.reset(); return result;
}
// 重置密码。
Result<> AccountService::resetPassword(const std::string& user, const std::string& email, const std::string& code, const std::string& password) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState,"请等待安全连接或当前请求完成");
    if (!passwordResetSupported_) return accountError(ErrorCode::NotImplemented,"服务器暂不支持邮箱重置密码");
    if (!validCredentials(user,password) || cc::auth::normalizedEmail(email).empty() || code.size()!=6)
        return accountError(ErrorCode::InvalidArgument,"请填写用户名、绑定邮箱、六位验证码及至少六位的新密码");
    auto sent=net_.send(cc::proto::withV2({cc::proto::MsgType::PasswordReset,"",{{"username",user},{"email",email},{"email_code",code},{"password",password}}}));
    if (sent) { resetPending_=true; codeResult_.reset(); deadlineMs_=clock_()+30000; }
    return sent;
}
// 通过已建立的 TLS 连接提交密码，等待服务端返回认证结果。
Result<> AccountService::login(const std::string& user, const std::string& password) {
    if (phase_ != AccountPhase::Connected || codePending_ || resetPending_) return accountError(ErrorCode::InvalidState, "当前不可登录，请等待连接或当前请求完成");
    if (!passwordLoginSupported_) return accountError(ErrorCode::ProtocolError, "服务器尚未升级密码登录，请联系管理员");
    if (password.size()<6 || password.size()>1024 || (user!=loginAlias_ && !validCredentials(user,password))) return accountError(ErrorCode::InvalidArgument, "用户名、邮箱或密码格式无效");
    auto snapshot = store_.load();
    if (!snapshot) return Result<>::failure(snapshot.error());
    if (snapshot.value().account.username != user && user!=loginAlias_) return accountError(ErrorCode::InvalidArgument, "账号与本地存储不匹配");
    auto request = cc::proto::withV2(cc::proto::makeLoginRequest(user, password));
    request.body["chat_mode"] = "ordinary-v1";
    auto sent = net_.send(request);
    // 传输层已经复制出站帧，账号服务无需继续保留请求中的密码。
    auto& submittedPassword = request.body["password"].get_ref<std::string&>();
    OPENSSL_cleanse(submittedPassword.data(), submittedPassword.size());
    if (!sent) return sent;
    beginWait(AccountPhase::LoginPending);
    return Result<>::success({});
}
// 用绑定邮箱和验证码提交登录，等待服务器确认账号身份。
Result<> AccountService::loginWithEmailCode(const std::string& email,const std::string& code) {
    if(phase_!=AccountPhase::Connected || codePending_ || resetPending_ || !emailLoginSupported_)return accountError(ErrorCode::InvalidState,"服务器暂不支持邮箱验证码登录");
    if(cc::auth::normalizedEmail(email).empty() || code.size()!=6 || !std::all_of(code.begin(),code.end(),[](char c){return c>='0'&&c<='9';}))return accountError(ErrorCode::InvalidArgument,"请填写邮箱和六位验证码");
    auto sent=net_.send(cc::proto::withV2({cc::proto::MsgType::EmailLogin,"",{{"email",email},{"email_code",code}}}));
    if (!sent) return sent;
    beginWait(AccountPhase::LoginPending);
    return Result<>::success({});
}
// 按当前认证阶段接收服务器响应，处理验证码或登录结果。
Result<> AccountService::handle(const cc::proto::Message& message) {
    using namespace cc::proto;
    if (auto timeout = checkTimeout(); !timeout) return timeout;
    // 构造当前分支的失败结果或拒绝响应，供调用处统一返回。
    auto invalid = [this] {
        deadlineMs_ = 0;
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
        passwordLoginSupported_ = bodyBool(message.body, "password_login");
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
    if (message.type == MsgType::LoginResult && phase_ == AccountPhase::LoginPending) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean()) return invalid();
        deadlineMs_ = 0;
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
// 通知监听方连接发生变化，供界面或调用方刷新。
void AccountService::connectionChanged(bool connected) noexcept {
    passwordLoginSupported_ = false;
    deadlineMs_ = 0;
    userId_ = 0; uidLookupSupported_ = false; emailLoginSupported_=false; registrationEmailSupported_ = false; codePending_ = false; codeResult_.reset();
    passwordResetSupported_=false; resetPending_=false;
    phase_ = connected ? AccountPhase::TransportConnected : AccountPhase::Disconnected;
}
}
