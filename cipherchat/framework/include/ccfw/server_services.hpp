#pragma once
// 服务端接口不接收 IdentityState/ClientSnapshot/ChatRecord，只有账号元数据和密文。
#include "result.hpp"
#include "common/auth/login_proof.hpp"
#include "common/base64.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ccfw::server {
struct Challenge {
    std::string connectionId;
    std::string username;
    cc::Bytes nonce;
    std::int64_t expiresAtMs = 0;
};
// 登录挑战专用限额：至多 1024 条待验证记录，每条有效 60 秒。
inline constexpr std::size_t kMaxPendingAuth = 1024;
inline constexpr std::int64_t kLoginChallengeLifetimeMs = 60'000;

// 登录挑战簿：challenge 绑定 connectionId + username，nonce 一次性。
// 无论后续认证成功与否，consume 只取一次；断线即清理。
// 头文件内联实现保持单一来源，原项目 router 直接复用并在路由锁内访问。
class ChallengeBook {
public:
    Result<> issue(const Challenge& c) {
        if (c.connectionId.empty() || c.username.empty() ||
            c.nonce.size() != cc::auth::kLoginNonceLen) {
            return Result<>::failure(
                {ErrorCode::InvalidArgument, "M2-02", "挑战字段不完整"});
        }
        if (pending_.size() >= kMaxPendingAuth &&
            pending_.find(c.connectionId) == pending_.end()) {
            return Result<>::failure(
                {ErrorCode::Forbidden, "M2-02", "待认证连接数已达上限"});
        }
        pending_[c.connectionId] = c;  // 同连接重复请求：替换旧挑战
        return Result<>::success(Unit{});
    }
    // 时间由调用者注入以便测试；无论认证后续成功与否，只消费一次。
    Result<Challenge> consume(const std::string& connectionId,
                              std::int64_t nowMs) {
        auto it = pending_.find(connectionId);
        if (it == pending_.end()) {
            return Result<Challenge>::failure(
                {ErrorCode::NotFound, "M2-02", "该连接没有待验证的挑战"});
        }
        Challenge c = std::move(it->second);
        pending_.erase(it);  // 一次性：取出即焚
        if (nowMs >= c.expiresAtMs) {
            return Result<Challenge>::failure(
                {ErrorCode::Timeout, "M2-02", "挑战已过期"});
        }
        return Result<Challenge>::success(std::move(c));
    }
    // 在签发新挑战前调用；过期的连接不能一直占用待认证名额。
    void purgeExpired(std::int64_t nowMs) noexcept {
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (nowMs >= it->second.expiresAtMs) it = pending_.erase(it);
            else ++it;
        }
    }
    void disconnect(const std::string& connectionId) noexcept {
        pending_.erase(connectionId);
    }
    void revokeAccount(const std::string& username) noexcept {
        std::erase_if(pending_, [&](const auto& entry) { return entry.second.username == username; });
    }

private:
    std::map<std::string, Challenge> pending_;
};
struct PendingDelivery {
    std::string recipient;
    std::string sender;
    std::string messageId;
    std::string ciphertextJson; // 不含明文、私钥、文件密钥
};
class IDeliveryQueue {
public:
    virtual ~IDeliveryQueue() = default;
    virtual Result<> enqueue(const PendingDelivery&) = 0; // 按 recipient+messageId 幂等
    virtual Result<std::vector<PendingDelivery>> peek(const std::string& recipient,
                                                     std::size_t limit) = 0;
    // 仅接收方认证后的 ACK 才删除；socket write 成功不算签收。
    virtual Result<> acknowledge(const std::string& authenticatedRecipient,
                                 const std::string& messageId) = 0;
};
class SqliteDeliveryQueue final : public IDeliveryQueue {
public:
    SqliteDeliveryQueue();
    ~SqliteDeliveryQueue() override;
    Result<> open(const std::string& databasePath);
    Result<> enqueue(const PendingDelivery&) override;
    Result<std::vector<PendingDelivery>> peek(const std::string&, std::size_t) override;
    Result<> acknowledge(const std::string&, const std::string&) override;
    Result<std::optional<PendingDelivery>> find(const std::string& recipient,
                                               const std::string& messageId);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class IGroupDirectory {
public:
    virtual ~IGroupDirectory() = default;
    virtual Result<std::vector<std::string>> members(std::int64_t groupId) = 0;
};
class GroupAuthorizer {
public:
    explicit GroupAuthorizer(IGroupDirectory& groups) : groups_(groups) {}
    Result<> authorize(std::int64_t groupId, const std::string& authenticatedSender,
                       const std::string& recipient);
private:
    IGroupDirectory& groups_;
};
} // namespace ccfw::server
