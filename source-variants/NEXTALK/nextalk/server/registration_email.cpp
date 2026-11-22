#include "server/registration_email.hpp"
#include "common/auth/email.hpp"
#include "common/crypto/crypto.hpp"
#include <openssl/crypto.h>
#include <chrono>

namespace cc::server {
RegistrationEmail::RegistrationEmail(Sender sender, Clock clock)
    : sender_(std::move(sender)), clock_(clock ? std::move(clock) : Clock{[] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }}) {}
RegistrationEmail::Result RegistrationEmail::request(const std::string& username, const std::string& input) {
    const auto email = auth::normalizedEmail(input);
    if (email.empty()) return {false, "请输入有效的邮箱地址"};
    if (!sender_) return {false, "服务器尚未配置验证码邮件服务，请联系管理员"};
    const auto now = clock_();
    std::string code;
    Bytes salt;
    {
        std::lock_guard lock(mutex_);
        std::erase_if(entries_, [&](const auto& pair) { return !pair.second.sending && now - pair.second.sentAt >= 3600000; });
        while (!globalSends_.empty() && now - globalSends_.front() >= 3600000) globalSends_.pop_front();
        auto found = entries_.find(email);
        if (found != entries_.end() && (found->second.sending || now - found->second.sentAt < 60000))
            return {false, "验证码请求过于频繁，请稍后重试", int(std::max<std::int64_t>(1, (60000 - (now - found->second.sentAt) + 999) / 1000))};
        if (globalSends_.size() >= 20 || entries_.size() >= 4096) return {false, "邮件发送额度暂已用尽，请稍后重试", 3600};
        auto& entry = entries_[email];
        if (!entry.sends || now - entry.hourStart >= 3600000) { entry.hourStart = now; entry.sends = 0; }
        if (entry.sends >= 5) return {false, "此邮箱请求次数过多，请一小时后重试", 3600};
        // Rejection sampling avoids biased six-digit values.
        std::uint32_t value;
        do { auto bytes = crypto::randomBytes(4); value = 0; for (unsigned char b : bytes) value = (value << 8) | b; } while (value >= 4294000000u);
        code = std::to_string(1000000u + value % 1000000u).substr(1);
        salt = crypto::randomBytes(32);
        entry.username = username; entry.salt = salt; entry.digest.clear();
        entry.sentAt = now; entry.expires = 0; entry.attempts = 0; entry.sending = true; ++entry.sends;
        globalSends_.push_back(now);
    }
    // SMTP runs outside both the router lock and this lock: chat traffic keeps flowing.
    std::string error;
    try { error = sender_(email, code); } catch (...) { error = "邮件发送失败，请稍后重试"; }
    {
        std::lock_guard lock(mutex_);
        auto& entry = entries_.at(email);
        entry.sending = false;
        if (error.empty()) { entry.digest = crypto::sha256(salt + code); entry.expires = clock_() + 600000; }
    }
    OPENSSL_cleanse(code.data(), code.size());
    return {error.empty(), error.empty() ? "" : "邮件发送失败，请稍后重试或联系管理员", 60};
}
RegistrationEmail::Result RegistrationEmail::consume(const std::string& username, const std::string& input, const std::string& code) {
    const auto email = auth::normalizedEmail(input);
    std::lock_guard lock(mutex_);
    auto found = entries_.find(email);
    if (email.empty() || found == entries_.end()) return {false, "请先向注册邮箱发送验证码"};
    auto& e = found->second;
    if (e.sending || e.digest.empty() || e.username != username || clock_() >= e.expires || e.attempts >= 5)
        return {false, "验证码已失效，请重新获取"};
    ++e.attempts;
    const bool shape = code.size() == 6 && std::all_of(code.begin(),code.end(),[](char c){ return c >= '0' && c <= '9'; });
    auto digest = shape ? crypto::sha256(e.salt + code) : Bytes{};
    if (!shape || CRYPTO_memcmp(digest.data(), e.digest.data(), digest.size()) != 0) {
        if (e.attempts >= 5) e.digest.clear();
        return {false, e.attempts >= 5 ? "验证码错误次数过多，请重新获取" : "邮箱验证码错误"};
    }
    e.digest.clear();
    return {true, ""};
}
}
