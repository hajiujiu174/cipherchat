#pragma once
#include "common/base64.hpp"
#include <string>

namespace cc::server {
// The SMTP transport and branding regression tests use this same MIME renderer.
// 返回smtp_message 模块使用的verification邮件。
inline std::string verificationMail(const std::string& from, const std::string& recipient,
                                    const std::string& code, bool passwordReset, bool emailLogin) {
    const std::string purpose = emailLogin ? "邮箱登录" : passwordReset ? "重置密码" : "账号注册";
    const auto subject = cc::base64Encode(emailLogin ? "NEXTALK 邮箱登录验证码" :
        passwordReset ? "NEXTALK 重置密码验证码" : "NEXTALK 注册验证码");
    const auto body = cc::base64Encode("你的 NEXTALK " + purpose + "验证码是：" + code +
        "\r\n\r\n验证码 10 分钟内有效，仅用于本次" + purpose +
        "。请勿向他人透露。\r\n如果这不是你的操作，请忽略此邮件，你的密码不会因此改变。\r\n");
    std::string wrapped;
    for (std::size_t i = 0; i < body.size(); i += 76) wrapped += body.substr(i, 76) + "\r\n";
    return "From: <" + from + ">\r\nTo: <" + recipient + ">\r\nSubject: =?UTF-8?B?" + subject +
        "?=\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\n" + wrapped;
}
}
