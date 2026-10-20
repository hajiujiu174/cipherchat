#include "server/smtp_mailer.hpp"
#include "common/auth/email.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <memory>
#include <cstring>
#include <cstdio>

namespace cc::server {
namespace {
std::string environment(const char* key) { const auto* value = std::getenv(key); return value ? value : ""; }
struct Upload { std::string message; std::size_t offset = 0; };
std::size_t readMail(char* data, std::size_t size, std::size_t count, void* opaque) {
    auto& mail = *static_cast<Upload*>(opaque);
    const auto n = std::min(size * count, mail.message.size() - mail.offset);
    std::memcpy(data, mail.message.data() + mail.offset, n); mail.offset += n; return n;
}
std::string wrappedBase64(const std::string& text) {
    const auto encoded = cc::base64Encode(text); std::string result;
    for (std::size_t i=0; i<encoded.size(); i+=76) result += encoded.substr(i,76) + "\r\n";
    return result;
}
}
RegistrationEmail::Sender smtpSenderFromEnvironment(bool passwordReset, bool emailLogin) {
    const auto host = environment("CIPHERCHAT_SMTP_HOST"), user = environment("CIPHERCHAT_SMTP_USER"),
        password = environment("CIPHERCHAT_SMTP_PASSWORD"), from = auth::normalizedEmail(environment("CIPHERCHAT_SMTP_FROM")),
        ca = environment("CIPHERCHAT_SMTP_CA_FILE");
    auto port = environment("CIPHERCHAT_SMTP_PORT"); if (port.empty()) port = "465";
    if (host.empty() || user.empty() || password.empty() || from.empty()) return {};
    if ((port != "465" && port != "587") || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos)
        throw std::runtime_error("Invalid SMTP host/port; use a DNS host and TLS port 465 or STARTTLS port 587");
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("Cannot initialize SMTP transport");
    return [host, port, user, password, from, ca, passwordReset, emailLogin](const std::string& recipient, const std::string& code) -> std::string {
        if (auth::normalizedEmail(recipient).empty()) return "Invalid recipient";
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl) return "SMTP initialization failed";
        const auto url = std::string(port == "465" ? "smtps://" : "smtp://") + host + ":" + port;
        const auto purpose = std::string(emailLogin ? "邮箱登录" : passwordReset ? "重置密码" : "账号注册");
        const auto subject = cc::base64Encode(emailLogin ? "CipherChat 邮箱登录验证码" : passwordReset ? "CipherChat 重置密码验证码" : "CipherChat 注册邮箱验证码");
        Upload mail{"From: <" + from + ">\r\nTo: <" + recipient + ">\r\nSubject: =?UTF-8?B?" + subject + "?=\r\n"
            "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
            wrappedBase64("你的 CipherChat " + purpose + "验证码是：" + code + "\r\n\r\n验证码 10 分钟内有效，仅用于本次" + purpose + "。请勿向他人透露。\r\n如果这不是你的操作，请忽略此邮件，你的密码不会因此改变。\r\n")};
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> recipients(curl_slist_append(nullptr, recipient.c_str()), curl_slist_free_all);
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_USERNAME, user.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_PASSWORD, password.c_str());
        // QQ advertises several SASL mechanisms; explicitly use its supported password flow.
        curl_easy_setopt(curl.get(), CURLOPT_LOGIN_OPTIONS, "AUTH=LOGIN");
        curl_easy_setopt(curl.get(), CURLOPT_MAIL_FROM, from.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_MAIL_RCPT, recipients.get());
        curl_easy_setopt(curl.get(), CURLOPT_USE_SSL, long(CURLUSESSL_ALL));
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        if (!ca.empty()) curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, readMail);
        curl_easy_setopt(curl.get(), CURLOPT_READDATA, &mail);
        curl_easy_setopt(curl.get(), CURLOPT_UPLOAD, 1L);
        const auto status = curl_easy_perform(curl.get());
        if (status == CURLE_OK) return "";
        long response = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response);
        // Numeric transport/status codes only; never log AUTH exchanges, credentials or mail bodies.
        std::fprintf(stderr, "[server] SMTP submission failed (curl=%d, smtp=%ld)\n", int(status), response);
        return "SMTP submission failed";
    };
}
}
