#pragma once
#include "ccfw/types.hpp"
#include <chrono>
#include <stdexcept>

namespace ccfw::wire {
inline void require(bool condition) { if (!condition) throw std::runtime_error("invalid protocol payload"); }
inline std::string text(const nlohmann::json& json, const char* key, std::size_t max, bool empty = false) {
    const auto& value = json.at(key); require(value.is_string());
    auto result = value.get<std::string>(); require(result.size() <= max && (empty || !result.empty())); return result;
}
inline std::int64_t integer(const nlohmann::json& json, const char* key, std::int64_t max = INT64_MAX) {
    const auto& value = json.at(key); require(value.is_number_integer() && value >= 0 && value <= max);
    return value.get<std::int64_t>();
}
inline cc::Bytes bytes(const nlohmann::json& json, const char* key, std::size_t max, bool empty = false) {
    cc::Bytes result; require(cc::base64Decode(text(json, key, max * 2 + 4, empty), result));
    require(result.size() <= max && (empty || !result.empty())); return result;
}
inline bool kind(const std::string& kind) { return kind == "chat" || kind == "file_meta" || kind == "file_ack"; }
inline bool currentFileMessage(const ClientSnapshot& snapshot, const std::string& kind, const std::string& id) {
    if (kind != "file_meta" && kind != "file_ack" && kind != "file_chunk") return true;
    for (const auto& t : snapshot.transfers) if (t.transportVersion != 3) {
        if (id == "fmeta:" + t.fileId || id.starts_with("fack:" + t.fileId + ":") || id.starts_with("fchunk:" + t.fileId + ":")) return false;
    }
    return true;
}
inline std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline std::string id() { return cc::base64Encode(cc::crypto::randomBytes(16)); }
inline cc::proto::Message send(const WireEnvelope& envelope) {
    return cc::proto::withV2(cc::proto::makeSendMsg(envelope.messageId, envelope.to, envelope.kind, envelope.payloadBase64, envelope.groupId));
}
inline WireEnvelope envelope(const cc::proto::Message& message) {
    require(cc::proto::isV2(message));
    require(!message.msgId.empty() && message.msgId.size() <= 128);
    return {message.msgId, text(message.body, "from", 32), text(message.body, "to", 32), text(message.body, "kind", 32),
        text(message.body, "payload", 7 * 1024 * 1024), message.body.contains("group_id") ? integer(message.body, "group_id") : 0,
        message.body.contains("ts") ? integer(message.body, "ts") : 0};
}
inline cc::ratchet::X3dhPeerBundle bundle(const cc::proto::PrekeyBundle& encoded) {
    cc::ratchet::X3dhPeerBundle decoded;
    require(!encoded.username.empty() && encoded.username.size() <= 32);
    require(cc::base64Decode(encoded.signPub, decoded.signPub) && decoded.signPub.size() == 32);
    require(cc::base64Decode(encoded.dhPub, decoded.ikPub) && decoded.ikPub.size() == 32);
    require(cc::base64Decode(encoded.signedPrekey, decoded.spkPub) && decoded.spkPub.size() == 32);
    require(cc::base64Decode(encoded.spkSig, decoded.spkSig) && decoded.spkSig.size() == 64);
    if (encoded.otp) {
        require(encoded.otp->id > 0 && cc::base64Decode(encoded.otp->pub, decoded.opkPub) && decoded.opkPub.size() == 32);
        decoded.opkId = encoded.otp->id;
    }
    require(cc::crypto::ed25519Verify(decoded.signPub, decoded.spkPub, decoded.spkSig));
    return decoded;
}
} // namespace ccfw::wire
