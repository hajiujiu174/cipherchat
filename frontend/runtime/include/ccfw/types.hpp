#pragma once
#include "file_flow.hpp"
// 边界类型。继续使用原 common 的密钥/信封/协议类型，不另造密码算法。
#include "common/crypto/crypto.hpp"
#include "common/protocol/messages.hpp"
#include "common/ratchet/double_ratchet.hpp"
#include "common/ratchet/x3dh.hpp"
#include "ccfw/feature_types.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ccfw {
struct AccountScope {
    std::string server;   // 经 TLS 认证的服务器标识；不同服务器的同名账号隔离
    std::string username;
};
struct ConnectionOptions {
    std::string host = "localhost";
    std::uint16_t port = 7443;
    std::filesystem::path caFile;
    std::string expectedServerName = "localhost";
    std::uint32_t timeoutMs = 5000;
};
struct IdentityState {
    cc::crypto::Ed25519KeyPair signing;
    cc::crypto::X25519KeyPair identityDh;
    cc::crypto::X25519KeyPair signedPrekey;
    cc::Bytes signedPrekeySignature;
    std::map<std::int64_t, cc::Bytes> oneTimePrivateKeys;
    std::int64_t nextPrekeyId = 1;
};
struct PeerIdentity {
    std::string username;
    cc::Bytes signingPublicKey;
    cc::Bytes dhPublicKey;
    bool manuallyVerified = false;
};
enum class DeliveryState { Queued, ServerAccepted, RecipientReceived, Failed };
struct PlainEnvelope {
    std::string messageId;
    std::string logicalGroupMessageId;
    std::string from;
    std::string to;
    std::string kind = "chat"; // chat / file_meta / file_ack；决定 plaintext 的解析方式
    cc::Bytes plaintext;
    std::int64_t groupId = 0;
    std::int64_t timestamp = 0;
    nlohmann::json extensions = nlohmann::json::object();
};
struct ChatRecord {
    std::string messageId;
    std::string logicalGroupMessageId; // 群发送时各收件人共享的去重标识
    std::string from;
    std::string to;
    std::string text; // Ordinary message text in local history and TLS relay payloads.
    std::int64_t groupId = 0;
    std::int64_t timestamp = 0;
    DeliveryState state = DeliveryState::Queued;
    nlohmann::json extensions = nlohmann::json::object();
};
struct SessionRecord {
    std::string peer;
    std::string sessionId;
    cc::Bytes associatedData;
    nlohmann::json ratchetState; // 含秘密，仅可在客户端加密存储
    std::optional<cc::ratchet::Header> pendingPrekey;
};
struct TransferCheckpoint : nextalk::chat::FileState {
    std::filesystem::path sourceOrPartialPath;
    std::filesystem::path finalPath;
    std::optional<WireEnvelope> lastAck;

    TransferCheckpoint() = default;

    // 保留迁移前用于构造初始检查点的字段顺序。
    TransferCheckpoint(std::string id, std::string other, std::filesystem::path source,
                       std::uint64_t size, std::uint32_t contiguous, cc::Bytes key,
                       cc::Bytes tag, std::string digest)
        : sourceOrPartialPath(std::move(source)) {
        fileId = std::move(id);
        peer = std::move(other);
        totalBytes = size;
        nextContiguousChunk = contiguous;
        fileKey = std::move(key);
        previousTag = std::move(tag);
        sourceDigest = std::move(digest);
    }
};
struct GroupSnapshot {
    std::int64_t id = 0;
    std::uint64_t revision = 0;
    std::string name;
    std::vector<std::string> members;
};
struct MemberSendResult {
    std::string recipient;
    std::optional<std::string> messageId;
    std::string error;
};
struct GroupSendReport {
    std::string logicalMessageId;
    std::vector<MemberSendResult> members;
};
struct GroupDraft {
    std::int64_t groupId = 0;
    std::string text;
    GroupSendReport report;
};
struct ClientSnapshot {
    AccountScope account;
    std::uint64_t revision = 0;
    std::optional<IdentityState> identity;
    std::vector<PeerIdentity> trustedPeers;
    std::vector<SessionRecord> sessions;
    std::vector<ChatRecord> history;
    std::vector<WireEnvelope> outbox;
    std::vector<ReceivedEnvelope> receivedMessages; // 有界、绑定密文和路由的去重记录
    std::vector<PlainEnvelope> pendingPayloads; // 非 chat 解密后暂存，消费前崩溃可恢复
    std::vector<TransferCheckpoint> transfers;
    std::vector<PlainEnvelope> pendingSends; // 尚待身份确认/公钥束的发送请求，受本地加密保护
    std::vector<GroupSnapshot> groups;
    std::vector<GroupDraft> groupDrafts;
    FeatureState features;
};
struct SendText {
    std::string recipient;
    std::string text;
    std::int64_t groupId = 0;
    std::string logicalGroupMessageId;
    std::string messageId;
    nlohmann::json extensions = nlohmann::json::object();
};
struct FileProgress {
    std::string fileId;
    std::uint64_t authenticatedBytes = 0;
    std::uint64_t totalBytes = 0;
    bool complete = false;
    std::string peer;
    std::string name;
    std::filesystem::path path;
    bool outgoing = false;
    bool cancelled = false;
    std::uint8_t transportPath = 0;
    std::int64_t timestamp = 0;
    bool cacheUploaded = false;
    bool paused = false;
    std::int64_t conversationGroupId=0;
};
} // namespace ccfw
