#pragma once
#include "ccfw/types.hpp"
#include <limits>
#include <stdexcept>

// CBOR 在 DPAPI 保护之前保留原始字节；不会把私钥伪装成普通 UTF-8 字符串。
namespace ccfw {
namespace codec {
using Json = nlohmann::json;
// 返回snapshot_codec 模块使用的二进制数据。
inline Json binary(cc::BytesView bytes) { return Json::binary(std::vector<std::uint8_t>(bytes.begin(), bytes.end())); }
// 返回snapshot_codec 模块的字节数据。
inline cc::Bytes bytes(const Json& value) {
    if (!value.is_binary()) throw std::runtime_error("invalid snapshot bytes");
    const auto& data = value.get_binary();
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}
// 将文件系统路径转成便于保存的 UTF-8 文本。
inline std::string pathText(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
// 返回snapshot_codec 模块使用的路径。
inline std::filesystem::path path(const std::string& value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
// 确保存在snapshot_codec 模块。
inline void ensure(bool valid) { if (!valid) throw std::runtime_error("invalid client snapshot"); }
// 读取 JSON 中的非负整数并检查其取值范围。
inline std::uint64_t natural(const Json& value, std::uint64_t max = INT64_MAX) {
    ensure(value.is_number_integer() && value >= 0 && value <= max);
    return value.get<std::uint64_t>();
}
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AccountScope, server, username)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WireEnvelope, messageId, from, to, kind, payloadBase64, groupId, timestamp)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ReceivedEnvelope, messageId, from, to, kind, groupId, ciphertextSha256)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ChatRecord, messageId, logicalGroupMessageId, from, to, text, groupId, timestamp, state)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GroupSnapshot, id, revision, name, members)
// 把 MemberSendResult 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const MemberSendResult& m) {
    j = {{"recipient", m.recipient}, {"message_id", m.messageId ? nlohmann::json(*m.messageId) : nlohmann::json(nullptr)}, {"error", m.error}};
}
// 从 JSON 恢复 MemberSendResult 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, MemberSendResult& m) {
    m.recipient = j.at("recipient").get<std::string>(); m.error = j.at("error").get<std::string>();
    if (!j.at("message_id").is_null()) m.messageId = j.at("message_id").get<std::string>();
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GroupSendReport, logicalMessageId, members)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GroupDraft, groupId, text, report)
// 把 PeerIdentity 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const PeerIdentity& p) {
    j = {{"username", p.username}, {"signing", codec::binary(p.signingPublicKey)},
         {"dh", codec::binary(p.dhPublicKey)}, {"verified", p.manuallyVerified}};
}
// 从 JSON 恢复 PeerIdentity 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, PeerIdentity& p) {
    p.username = j.at("username").get<std::string>();
    p.signingPublicKey = codec::bytes(j.at("signing"));
    p.dhPublicKey = codec::bytes(j.at("dh"));
    p.manuallyVerified = j.at("verified").get<bool>();
    codec::ensure(p.signingPublicKey.size() == 32 && p.dhPublicKey.size() == 32);
}
// 把 IdentityState 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const IdentityState& identity) {
    // 获取snapshot_codec 模块对应的 pair，供调用方读取或参与计算。
    auto pair = [](const auto& key) { return nlohmann::json{{"private", codec::binary(key.priv)}, {"public", codec::binary(key.pub)}}; };
    j = {{"signing", pair(identity.signing)}, {"dh", pair(identity.identityDh)},
         {"spk", pair(identity.signedPrekey)}, {"signature", codec::binary(identity.signedPrekeySignature)},
         {"next_prekey", identity.nextPrekeyId}, {"one_time", nlohmann::json::array()}};
    for (const auto& [id, key] : identity.oneTimePrivateKeys) j["one_time"].push_back({{"id", id}, {"key", codec::binary(key)}});
}
// 从 JSON 恢复 IdentityState 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, IdentityState& identity) {
    // 完成snapshot_codec 模块内部的pair步骤，供外层流程调用。
    auto pair = [](const nlohmann::json& json, auto& key) {
        key.priv = codec::bytes(json.at("private")); key.pub = codec::bytes(json.at("public"));
        codec::ensure(key.priv.size() == 32 && key.pub.size() == 32);
    };
    pair(j.at("signing"), identity.signing); pair(j.at("dh"), identity.identityDh);
    pair(j.at("spk"), identity.signedPrekey);
    identity.signedPrekeySignature = codec::bytes(j.at("signature"));
    codec::ensure(identity.signedPrekeySignature.size() == 64);
    identity.nextPrekeyId = static_cast<std::int64_t>(codec::natural(j.at("next_prekey")));
    codec::ensure(identity.nextPrekeyId > 0 && j.at("one_time").is_array() && j.at("one_time").size() <= 4096);
    for (const auto& key : j.at("one_time")) {
        const auto id = static_cast<std::int64_t>(codec::natural(key.at("id")));
        auto bytes = codec::bytes(key.at("key"));
        codec::ensure(id > 0 && id < identity.nextPrekeyId && bytes.size() == 32);
        codec::ensure(identity.oneTimePrivateKeys.emplace(id, std::move(bytes)).second);
    }
}
// 把 SessionRecord 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const SessionRecord& s) {
    j = {{"peer", s.peer}, {"id", s.sessionId}, {"aad", codec::binary(s.associatedData)}, {"ratchet", s.ratchetState},
         {"prekey", s.pendingPrekey ? s.pendingPrekey->toJson() : nlohmann::json(nullptr)}};
}
// 从 JSON 恢复 SessionRecord 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, SessionRecord& s) {
    s.peer = j.at("peer").get<std::string>(); s.sessionId = j.at("id").get<std::string>();
    s.associatedData = codec::bytes(j.at("aad")); s.ratchetState = j.at("ratchet");
    codec::ensure(!s.peer.empty() && !s.sessionId.empty() && s.associatedData.size() == 64);
    for (const auto* name : {"dhs_priv", "dhs_pub", "dhr", "rk", "cks", "ckr"}) {
        cc::Bytes key;
        const auto encoded = s.ratchetState.at(name).get<std::string>();
        codec::ensure(cc::base64Decode(encoded, key));
        const bool optional = std::string_view(name) == "dhr" || std::string_view(name) == "cks" || std::string_view(name) == "ckr";
        codec::ensure(key.size() == 32 || (optional && key.empty()));
    }
    for (const auto* name : {"ns", "nr", "pn"}) codec::natural(s.ratchetState.at(name), UINT32_MAX);
    const auto& skipped = s.ratchetState.at("skipped");
    codec::ensure(skipped.is_object() && skipped.size() <= 64);
    for (const auto& key : skipped) {
        cc::Bytes decoded;
        codec::ensure(cc::base64Decode(key.get<std::string>(), decoded) && decoded.size() == 32);
    }
    if (j.contains("prekey") && !j.at("prekey").is_null()) s.pendingPrekey = cc::ratchet::Header::fromJson(j.at("prekey"));
}
// 把 PlainEnvelope 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const PlainEnvelope& p) {
    j = {{"id", p.messageId}, {"logical_id", p.logicalGroupMessageId}, {"from", p.from}, {"to", p.to},
         {"kind", p.kind}, {"body", codec::binary(p.plaintext)}, {"group", p.groupId}, {"timestamp", p.timestamp}};
}
// 从 JSON 恢复 PlainEnvelope 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, PlainEnvelope& p) {
    p.messageId = j.at("id").get<std::string>(); p.logicalGroupMessageId = j.at("logical_id").get<std::string>();
    p.from = j.at("from").get<std::string>(); p.to = j.at("to").get<std::string>(); p.kind = j.at("kind").get<std::string>();
    p.plaintext = codec::bytes(j.at("body")); p.groupId = j.at("group").get<std::int64_t>(); p.timestamp = j.at("timestamp").get<std::int64_t>();
    p.extensions = j.value("extensions", nlohmann::json::object());
}
// 把 PendingFileChunk 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const PendingFileChunk& p) {
    j = {{"envelope", p.envelope}, {"tag", codec::binary(p.tag)}};
}
// 从 JSON 恢复 PendingFileChunk 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, PendingFileChunk& p) {
    p.envelope = j.at("envelope").get<WireEnvelope>(); p.tag = codec::bytes(j.at("tag"));
    codec::ensure(p.tag.size() == 16);
}
// 把 TransferCheckpoint 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const TransferCheckpoint& t) {
    j = {{"id", t.fileId}, {"peer", t.peer}, {"path", codec::pathText(t.sourceOrPartialPath)},
         {"bytes", t.totalBytes}, {"next_chunk", t.nextContiguousChunk}, {"previous_tag", codec::binary(t.previousTag)}, {"digest", t.sourceDigest},
         {"outgoing", t.outgoing}, {"metadata_queued", t.metadataQueued}, {"metadata_ack", t.metadataAcknowledged},
         {"complete", t.complete}, {"cancelled", t.cancelled}, {"final_path", codec::pathText(t.finalPath)},
         {"name", t.name}, {"chunk_count", t.chunkCount}, {"timestamp", t.timestamp},
         {"last_ack", t.lastAck ? nlohmann::json(*t.lastAck) : nlohmann::json(nullptr)}, {"partial_digest", t.partialDigest},
         {"transport_version", t.transportVersion}, {"pending_chunks", t.pendingChunks}, {"transport_path", t.serverCache ? 3 : t.path},
         {"server_cache", t.serverCache}, {"cache_uploaded", t.cacheUploaded},{"cache_copy",t.cacheCopy},{"paused",t.paused},{"conversation_group_id",t.conversationGroupId}};
}
// 从 JSON 恢复 TransferCheckpoint 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, TransferCheckpoint& t) {
    t.fileId = j.at("id").get<std::string>(); t.peer = j.at("peer").get<std::string>();
    t.timestamp = static_cast<std::int64_t>(codec::natural(j.value("timestamp", nlohmann::json(0)), INT64_MAX));
    t.conversationGroupId=j.value("conversation_group_id",std::int64_t{0});t.paused=j.value("paused",false); t.serverCache=j.value("server_cache",false); t.cacheUploaded=j.value("cache_uploaded",false);
    t.cacheCopy=j.value("cache_copy",false);
    t.sourceOrPartialPath = codec::path(j.at("path").get<std::string>());
    t.totalBytes = codec::natural(j.at("bytes"));
    t.nextContiguousChunk = static_cast<std::uint32_t>(codec::natural(j.at("next_chunk"), UINT32_MAX));
    t.fileKey = j.contains("key") ? codec::bytes(j.at("key")) : cc::Bytes{}; t.previousTag = codec::bytes(j.at("previous_tag")); t.sourceDigest = j.at("digest").get<std::string>();
    codec::ensure((t.fileKey.empty() || t.fileKey.size() == 32) && (t.previousTag.empty() || t.previousTag.size() == 16));
    if (j.contains("outgoing")) {
        t.outgoing = j.at("outgoing").get<bool>(); t.metadataQueued = j.at("metadata_queued").get<bool>();
        t.metadataAcknowledged = j.at("metadata_ack").get<bool>(); t.complete = j.at("complete").get<bool>();
        t.cancelled = j.at("cancelled").get<bool>(); t.finalPath = codec::path(j.at("final_path").get<std::string>());
        t.name = j.at("name").get<std::string>();
        t.chunkCount = static_cast<std::uint32_t>(codec::natural(j.at("chunk_count"), UINT32_MAX));
        if (!j.at("last_ack").is_null()) t.lastAck = j.at("last_ack").get<WireEnvelope>();
        t.partialDigest = j.value("partial_digest", std::string{});
        t.transportVersion = static_cast<std::uint32_t>(codec::natural(j.value("transport_version", nlohmann::json(2)), 3));
        codec::ensure(t.transportVersion == 2 || t.transportVersion == 3);
        if (j.contains("pending_chunks")) {
            codec::ensure(j.at("pending_chunks").is_array() && j.at("pending_chunks").size() <= 16);
            t.pendingChunks = j.at("pending_chunks").get<std::vector<PendingFileChunk>>();
        }
        codec::ensure(t.pendingChunks.empty() || (t.outgoing && t.transportVersion == 3));
        t.path = static_cast<std::uint8_t>(codec::natural(j.value("transport_path", nlohmann::json(0)), 4));
        codec::ensure(t.path <= 4 && (t.path != 4 || t.serverCache));
        if(t.serverCache)t.path=4; // Keep the serialized route readable by pre-cache clients.
        // Persisted routes 1 and 2 came from the retired direct channel. Resume through the server.
        if (t.path == 1 || t.path == 2) t.path = 3;
        for (std::size_t i = 0; i < t.pendingChunks.size(); ++i) {
            const auto& packet = t.pendingChunks[i].envelope;
            codec::ensure(packet.kind == "file_chunk" && packet.to == t.peer && packet.groupId == 0 && packet.payloadBase64.size() <= 128 * 1024);
            codec::ensure(packet.messageId == "fchunk:" + t.fileId + ":" + std::to_string(t.nextContiguousChunk + i));
            codec::ensure(t.nextContiguousChunk + i < t.chunkCount);
        }
        // Legacy task fixtures may lack a filename; real v2 checkpoints always have one.
        if (!t.name.empty()) {
            codec::ensure(t.totalBytes <= 1024ULL * 1024 * 1024 && t.chunkCount == (t.totalBytes + 65535) / 65536);
            codec::ensure(t.nextContiguousChunk <= t.chunkCount);
        }
    }
}
// 把 ClientSnapshot 转为 JSON 表示，与存储或传输格式对应。
inline void to_json(nlohmann::json& j, const ClientSnapshot& s) {
    j = {{"format", 2}, {"account", s.account}, {"revision", s.revision},
         {"history", s.history}, {"outbox", s.outbox},
         {"received", s.receivedMessages}, {"pending", s.pendingPayloads}, {"transfers", s.transfers}, {"pending_sends", s.pendingSends},
         {"groups", s.groups}, {"group_drafts", s.groupDrafts}};
}
// 从 JSON 恢复 ClientSnapshot 数据，与存储或传输格式对应。
inline void from_json(const nlohmann::json& j, ClientSnapshot& s) {
    codec::ensure(j.at("format").is_number_integer() && (j.at("format") == 1 || j.at("format") == 2));
    s.account = j.at("account").get<AccountScope>(); s.revision = codec::natural(j.at("revision"));
    codec::ensure(!s.account.server.empty() && !s.account.username.empty() && s.account.username.size() <= 32);
    s.identity.reset(); s.trustedPeers.clear(); s.sessions.clear();
    for (const auto* field : {"history", "outbox", "received", "pending", "transfers"}) codec::ensure(j.at(field).is_array());
    codec::ensure(j.at("received").size() <= 4096);
    s.history = j.at("history").get<std::vector<ChatRecord>>();
    s.outbox = j.at("outbox").get<std::vector<WireEnvelope>>();
    s.receivedMessages = j.at("received").get<std::vector<ReceivedEnvelope>>();
    s.pendingPayloads = j.at("pending").get<std::vector<PlainEnvelope>>();
    s.transfers = j.at("transfers").get<std::vector<TransferCheckpoint>>();
    s.pendingSends = j.value("pending_sends", std::vector<PlainEnvelope>{});
    s.groups = j.value("groups", std::vector<GroupSnapshot>{});
    s.groupDrafts = j.value("group_drafts", std::vector<GroupDraft>{});
    if (j.at("format") == 1) {
        s.outbox.clear(); s.pendingSends.clear(); s.pendingPayloads.clear(); s.receivedMessages.clear();
        for (auto& h : s.history) if(h.from==s.account.username && h.state==DeliveryState::Queued) h.state=DeliveryState::Failed;
        for (auto& t : s.transfers) { t.transportVersion=2; t.fileKey.clear(); t.previousTag.clear(); t.pendingChunks.clear(); t.lastAck.reset();  if(!t.complete)t.cancelled=true; }
    }
    for (const auto& h : s.history) codec::ensure(static_cast<int>(h.state) >= 0 && static_cast<int>(h.state) <= 3);
}
} // namespace ccfw
