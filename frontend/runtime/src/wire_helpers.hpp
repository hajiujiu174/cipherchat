#pragma once
#include "ccfw/types.hpp"
#include <chrono>
#include <stdexcept>

namespace ccfw::wire {
// 检查传入条件是否成立，失败时抛出当前模块的错误。
inline void require(bool condition) { if (!condition) throw std::runtime_error("invalid protocol payload"); }
// 返回wire_helpers 模块的文本。
inline std::string text(const nlohmann::json& json, const char* key, std::size_t max, bool empty = false) {
    const auto& value = json.at(key); require(value.is_string());
    auto result = value.get<std::string>(); require(result.size() <= max && (empty || !result.empty())); return result;
}
// 返回wire_helpers 模块使用的整数。
inline std::int64_t integer(const nlohmann::json& json, const char* key, std::int64_t max = INT64_MAX) {
    const auto& value = json.at(key); require(value.is_number_integer() && value >= 0 && value <= max);
    return value.get<std::int64_t>();
}
// 返回wire_helpers 模块的字节数据。
inline cc::Bytes bytes(const nlohmann::json& json, const char* key, std::size_t max, bool empty = false) {
    cc::Bytes result; require(cc::base64Decode(text(json, key, max * 2 + 4, empty), result));
    require(result.size() <= max && (empty || !result.empty())); return result;
}
// 获取wire_helpers 模块对应的 kind，供调用方读取或参与计算。
inline bool kind(const std::string& kind) { return nextalk::chat::supportedSessionKind(kind); }
// 返回wire_helpers 模块使用的当前有效的文件任务消息。
inline bool currentFileMessage(const ClientSnapshot& snapshot, const std::string& kind, const std::string& id) {
    for (const auto& transfer : snapshot.transfers) {
        if (!nextalk::chat::currentFileMessage(kind, id, transfer.fileId, transfer.transportVersion == 3))
            return false;
    }
    return true;
}
// 返回wire_helpers 模块使用的当前时间。
inline std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// 返回wire_helpers 模块使用的ID。
inline std::string id() { return cc::base64Encode(cc::crypto::randomBytes(16)); }
// 发送wire_helpers 模块。
inline cc::proto::Message send(const WireEnvelope& envelope) {
    return cc::proto::withV2(cc::proto::makeSendMsg(envelope.messageId, envelope.to, envelope.kind, envelope.payloadBase64, envelope.groupId));
}
// 返回wire_helpers 模块使用的消息信封。
inline WireEnvelope envelope(const cc::proto::Message& message) {
    require(cc::proto::isV2(message));
    require(!message.msgId.empty() && message.msgId.size() <= 128);
    return {message.msgId, text(message.body, "from", 32), text(message.body, "to", 32), text(message.body, "kind", 32),
        text(message.body, "payload", 7 * 1024 * 1024), message.body.contains("group_id") ? integer(message.body, "group_id") : 0,
        message.body.contains("ts") ? integer(message.body, "ts") : 0};
}
} // namespace ccfw::wire
