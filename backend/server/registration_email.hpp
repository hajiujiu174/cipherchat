#pragma once
#include "common/base64.hpp"
#include <deque>
#include <functional>
#include <map>
#include <mutex>

namespace cc::server {
class RegistrationEmail {
public:
    // Empty result means the SMTP server accepted the message, never mailbox delivery.
    using Sender = std::function<std::string(const std::string&, const std::string&)>;
    using Clock = std::function<std::int64_t()>;
    struct Result { bool ok = false; std::string error; int retryAfter = 0; };
    // 构造邮箱验证码服务，初始化后续操作需要的状态或依赖。
    explicit RegistrationEmail(Sender sender = {}, Clock clock = {});
    // 返回邮箱验证码服务的可用性。
    bool available() const { return bool(sender_); }
    // 校验邮箱与用途，按发送频率限制生成并投递限时验证码。
    Result request(const std::string& username, const std::string& email);
    // 核对用途、邮箱和验证码，成功后消费该验证码避免重复使用。
    Result consume(const std::string& username, const std::string& email, const std::string& code);
private:
    struct Entry {
        std::string username;
        Bytes salt, digest;
        std::int64_t sentAt = 0, expires = 0, hourStart = 0;
        int attempts = 0, sends = 0;
        bool sending = false;
    };
    Sender sender_;
    Clock clock_;
    std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    std::deque<std::int64_t> globalSends_;
};
}
