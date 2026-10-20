#include "ccfw/identity_card.hpp"
#include <algorithm>
#include <cctype>

namespace ccfw {
namespace {
Error invalid(const char* text) { return {ErrorCode::InvalidArgument, "M10-06", text}; }
bool valid(const PeerIdentity& p) {
    return !p.username.empty() && p.username.size() <= 32 && p.signingPublicKey.size() == 32 && p.dhPublicKey.size() == 32;
}
}
std::string identityFingerprint(const PeerIdentity& p) {
    if (!valid(p)) return {};
    const auto hash = cc::crypto::sha256(p.signingPublicKey + p.dhPublicKey);
    std::string hex; static constexpr char digits[] = "0123456789ABCDEF";
    for (const unsigned char c : hash) { hex += digits[c >> 4]; hex += digits[c & 15]; }
    return hex;
}
bool compareFingerprint(const PeerIdentity& p, const std::string& text) {
    if (text.size() > 1024 || !valid(p)) return false;
    std::string normalized;
    for (const unsigned char c : text) {
        if (std::isspace(c)) continue;
        if (!std::isxdigit(c)) return false;
        normalized += static_cast<char>(std::toupper(c));
    }
    return normalized.size() == 64 && cc::crypto::constantTimeEqual(normalized, identityFingerprint(p));
}
Result<std::string> exportIdentityCard(const IdentityCard& card) {
    if (!valid(card.identity) || card.scope.server.empty() || card.scope.server.size() > 512 || card.scope.username != card.identity.username)
        return Result<std::string>::failure(invalid("身份卡字段不完整"));
    nlohmann::json data = {{"version", 1}, {"server", card.scope.server}, {"username", card.identity.username},
        {"signing", cc::base64Encode(card.identity.signingPublicKey)}, {"dh", cc::base64Encode(card.identity.dhPublicKey)}};
    return Result<std::string>::success("CipherChatIdentity:1\n" + data.dump());
}
Result<IdentityCard> importIdentityCard(const std::string& text, const AccountScope& expected) {
    try {
        constexpr std::string_view prefix = "CipherChatIdentity:1\n";
        if (text.size() > 4096 || !text.starts_with(prefix)) return Result<IdentityCard>::failure(invalid("身份卡标记或长度无效"));
        const auto j = nlohmann::json::parse(text.substr(prefix.size()));
        if (!j.is_object() || j.size() != 5 || j.at("version") != 1) return Result<IdentityCard>::failure(invalid("身份卡版本或结构无效"));
        IdentityCard result; result.scope = {j.at("server").get<std::string>(), j.at("username").get<std::string>()};
        result.identity.username = result.scope.username;
        if (!cc::base64Decode(j.at("signing").get<std::string>(), result.identity.signingPublicKey) ||
            !cc::base64Decode(j.at("dh").get<std::string>(), result.identity.dhPublicKey) || !valid(result.identity))
            return Result<IdentityCard>::failure(invalid("身份公钥格式无效"));
        if (result.scope.server != expected.server || result.scope.username != expected.username)
            return Result<IdentityCard>::failure({ErrorCode::IdentityChanged, "M10-06", "身份卡的服务器作用域或联系人不匹配"});
        return Result<IdentityCard>::success(std::move(result));
    } catch (...) { return Result<IdentityCard>::failure(invalid("身份卡文本已损坏")); }
}
Result<> validateIdentityConfirmation(const PeerIdentity& displayed, const PeerIdentity& current) {
    if (!valid(displayed) || !valid(current) || displayed.username != current.username ||
        displayed.signingPublicKey != current.signingPublicKey || displayed.dhPublicKey != current.dhPublicKey)
        return Result<>::failure({ErrorCode::IdentityChanged, "M10-06", "公钥已变化，请重新读取身份卡并核对"});
    return Result<>::success({});
}
}
