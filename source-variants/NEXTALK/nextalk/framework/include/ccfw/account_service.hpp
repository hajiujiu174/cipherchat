#pragma once
#include "ccfw/local_store.hpp"
#include "ccfw/transport.hpp"
#include <chrono>
#include <functional>

namespace ccfw {
enum class AccountPhase { Disconnected, TransportConnected, Negotiating, Connected, Registering, ChallengePending, Online };
class AccountService {
public:
    using Clock = std::function<std::int64_t()>;
    AccountService(ITransport& net, ILocalStore& store, Clock clock = {});
    ~AccountService();
    Result<> negotiateProtocol(); // TLS 成功后协商 v2；成功响应后才允许账号请求
    void setLoginAlias(std::string alias, std::function<Result<>(const std::string&)> bind) { loginAlias_=std::move(alias); bindAccount_=std::move(bind); }
    Result<> registerAccount(const std::string& username, const std::string& password,
                             const std::string& email = {}, const std::string& emailCode = {});
    Result<> requestRegistrationCode(const std::string& username, const std::string& email, bool passwordReset = false, bool emailLogin = false);
    Result<> loginWithEmailCode(const std::string& email, const std::string& code);
    Result<> resetPassword(const std::string& username, const std::string& email, const std::string& code, const std::string& newPassword);
    std::optional<cc::proto::Message> takeRegistrationCodeResult();
    Result<> login(const std::string& username, const std::string& password);
    Result<> uploadPrekeys(std::size_t count);
    Result<> handle(const cc::proto::Message&); // 只消费账号相关响应
    AccountPhase phase() const noexcept { return phase_; }
    std::int64_t userId() const noexcept { return userId_; }
    bool uidLookupSupported() const noexcept { return uidLookupSupported_; }
    void connectionChanged(bool connected) noexcept;
    Result<> checkTimeout();
private:
    void clearPassword() noexcept;
    void beginWait(AccountPhase);
    ITransport& net_;
    ILocalStore& store_;
    AccountPhase phase_ = AccountPhase::Disconnected;
    Clock clock_;
    std::int64_t deadlineMs_ = 0;
    std::string password_; // 仅请求挑战期间保留，派生证明后立即擦除。
    bool responseSent_ = false;
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
