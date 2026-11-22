#pragma once
#include "ccfw/local_store.hpp"
#include "ccfw/transport.hpp"
#include <chrono>
#include <functional>

namespace ccfw {
enum class AccountPhase { Disconnected, TransportConnected, Negotiating, Connected, Registering, LoginPending, Online };
class AccountService {
public:
    using Clock = std::function<std::int64_t()>;
    // 构造客户端账号服务，初始化后续操作需要的状态或依赖。
    AccountService(ITransport& net, ILocalStore& store, Clock clock = {});
    // 发起应用层 Hello 协商以获得服务端协议版本和支持的功能。
    Result<> negotiateProtocol(); // TLS 成功后协商 v2；成功响应后才允许账号请求
    // 设置客户端账号服务使用的登录别名。
    void setLoginAlias(std::string alias, std::function<Result<>(const std::string&)> bind) { loginAlias_=std::move(alias); bindAccount_=std::move(bind); }
    // 校验注册凭据与邮箱验证码后，通过当前连接提交注册申请。
    Result<> registerAccount(const std::string& username, const std::string& password,
                             const std::string& email = {}, const std::string& emailCode = {});
    // 请求注册验证码。
    Result<> requestRegistrationCode(const std::string& username, const std::string& email, bool passwordReset = false, bool emailLogin = false);
    // 用绑定邮箱和验证码提交登录，等待服务器确认账号身份。
    Result<> loginWithEmailCode(const std::string& email, const std::string& code);
    // 重置密码。
    Result<> resetPassword(const std::string& username, const std::string& email, const std::string& code, const std::string& newPassword);
    // 取出注册验证码结果。
    std::optional<cc::proto::Message> takeRegistrationCodeResult();
    // 通过已建立的 TLS 连接提交密码，等待服务端返回认证结果。
    Result<> login(const std::string& username, const std::string& password);
    // 按当前认证阶段接收服务器响应，处理验证码或登录结果。
    Result<> handle(const cc::proto::Message&); // 只消费账号相关响应
    // 返回客户端账号服务的当前阶段。
    AccountPhase phase() const noexcept { return phase_; }
    // 返回客户端账号服务使用的用户ID。
    std::int64_t userId() const noexcept { return userId_; }
    // 返回客户端账号服务使用的UIDLookup受支持。
    bool uidLookupSupported() const noexcept { return uidLookupSupported_; }
    // 通知监听方连接发生变化，供界面或调用方刷新。
    void connectionChanged(bool connected) noexcept;
    // 检查超时，交给调用方区分有效结果与错误。
    Result<> checkTimeout();
private:
    // 开始Wait。
    void beginWait(AccountPhase);
    ITransport& net_;
    ILocalStore& store_;
    AccountPhase phase_ = AccountPhase::Disconnected;
    Clock clock_;
    std::int64_t deadlineMs_ = 0;
    bool passwordLoginSupported_ = false;
    bool registrationEmailSupported_ = false, codePending_ = false;
    bool passwordResetSupported_ = false, resetPending_ = false;
    std::optional<cc::proto::Message> codeResult_;
    std::int64_t userId_ = 0;
    bool uidLookupSupported_ = false;
    bool emailLoginSupported_ = false;
    std::string loginAlias_;
    std::function<Result<>(const std::string&)> bindAccount_;
};
} // namespace ccfw
