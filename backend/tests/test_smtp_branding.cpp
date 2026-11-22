#include <doctest/doctest.h>
#include "server/smtp_message.hpp"
#include <algorithm>

// 验证用例“NEXTALK verification mail brands registration password reset and email login”所描述的行为。
TEST_CASE("NEXTALK verification mail brands registration password reset and email login") {
    for (int purpose = 0; purpose < 3; ++purpose) {
        const auto mail = cc::server::verificationMail("sender@example.test", "user@example.test",
                                                       "123456", purpose == 1, purpose == 2);
        const auto marker = mail.find("Subject: =?UTF-8?B?");
        REQUIRE(marker != std::string::npos);
        const auto begin = marker + std::string("Subject: =?UTF-8?B?").size();
        const auto end = mail.find("?=\r\n", begin);
        REQUIRE(end != std::string::npos);
        cc::Bytes subject;
        REQUIRE(cc::base64Decode(mail.substr(begin, end - begin), subject));
        CHECK(subject == (purpose == 0 ? "NEXTALK 注册验证码" :
            purpose == 1 ? "NEXTALK 重置密码验证码" : "NEXTALK 邮箱登录验证码"));
        const auto separator = mail.find("\r\n\r\n");
        REQUIRE(separator != std::string::npos);
        auto encoded = mail.substr(separator + 4);
        std::erase(encoded, '\r'); std::erase(encoded, '\n');
        cc::Bytes body;
        REQUIRE(cc::base64Decode(encoded, body));
        CHECK(body.find("NEXTALK") != std::string::npos);
        CHECK(body.find("123456") != std::string::npos);
        CHECK(body.find(std::string("Cipher") + "Chat") == std::string::npos);
        CHECK(body.find(purpose == 0 ? "账号注册" : purpose == 1 ? "重置密码" : "邮箱登录") != std::string::npos);
    }
}
