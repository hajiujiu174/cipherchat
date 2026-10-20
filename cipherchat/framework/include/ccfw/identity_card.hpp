#pragma once
#include "ccfw/result.hpp"
#include "ccfw/types.hpp"

namespace ccfw {
// Public material only. Import never changes trust and never follows a URL.
struct IdentityCard { AccountScope scope; PeerIdentity identity; };
std::string identityFingerprint(const PeerIdentity&);
bool compareFingerprint(const PeerIdentity&, const std::string&);
Result<std::string> exportIdentityCard(const IdentityCard&);
Result<IdentityCard> importIdentityCard(const std::string&, const AccountScope& expected);
Result<> validateIdentityConfirmation(const PeerIdentity& displayed, const PeerIdentity& current);
}
