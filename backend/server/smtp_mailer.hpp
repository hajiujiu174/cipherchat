#pragma once
#include "server/registration_email.hpp"
namespace cc::server {
// Credentials are read only by the server process, never by a client.
// 返回smtp_mailer 模块使用的smtp发送者From环境变量。
RegistrationEmail::Sender smtpSenderFromEnvironment(bool passwordReset = false, bool emailLogin = false);
}
