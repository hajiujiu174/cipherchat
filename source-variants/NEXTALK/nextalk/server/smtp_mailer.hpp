#pragma once
#include "server/registration_email.hpp"
namespace cc::server {
// Credentials are read only by the server process, never by a client.
RegistrationEmail::Sender smtpSenderFromEnvironment(bool passwordReset = false, bool emailLogin = false);
}
