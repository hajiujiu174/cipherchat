#pragma once
#include "ccfw/result.hpp"
#include "ccfw/types.hpp"

namespace ccfw {
// Public material only. Import never changes trust and never follows a URL.
struct IdentityCard { AccountScope scope; PeerIdentity identity; };
// 返回测试使用的身份Fingerprint。
std::string identityFingerprint(const PeerIdentity&);
// 对比Fingerprint。
bool compareFingerprint(const PeerIdentity&, const std::string&);
// 导出身份卡片。
Result<std::string> exportIdentityCard(const IdentityCard&);
// 导入身份卡片。
Result<IdentityCard> importIdentityCard(const std::string&, const AccountScope& expected);
// 验证身份Confirmation，交给调用方区分有效结果与错误。
Result<> validateIdentityConfirmation(const PeerIdentity& displayed, const PeerIdentity& current);
}
