#pragma once
// 边界类型。继续使用原 common 的密钥/信封/协议类型，不另造密码算法。
#include "common/crypto/crypto.hpp"
#include "common/protocol/messages.hpp"
#include "common/ratchet/double_ratchet.hpp"
#include "common/ratchet/x3dh.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
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
struct WireEnvelope {
    std::string messageId;
    std::string from;
    std::string to;
    std::string kind = "chat";
    std::string payloadBase64; // 这里只允许密文/加密文件信封
    std::int64_t groupId = 0;
    std::int64_t timestamp = 0;
};
struct DirectFileEndpoint {
    std::string fileId;
    std::vector<std::string> hosts;
    std::uint16_t port = 0;
    std::string token;
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
};
struct ChatRecord {
    std::string messageId;
    std::string logicalGroupMessageId; // 群发送时各收件人共享的去重标识
    std::string from;
    std::string to;
    std::string text; // 仅客户端内存可见；本地存储须加密，禁止交给服务器
    std::int64_t groupId = 0;
    std::int64_t timestamp = 0;
    DeliveryState state = DeliveryState::Queued;
};
struct SessionRecord {
    std::string peer;
    std::string sessionId;
    cc::Bytes associatedData;
    nlohmann::json ratchetState; // 含秘密，仅可在客户端加密存储
    std::optional<cc::ratchet::Header> pendingPrekey;
};
struct PendingFileChunk {
    WireEnvelope envelope;
    cc::Bytes tag;
};
struct TransferCheckpoint {
    std::string fileId;
    std::string peer;
    std::filesystem::path sourceOrPartialPath;
    std::uint64_t totalBytes = 0;
    std::uint32_t nextContiguousChunk = 0;
    cc::Bytes fileKey; // 随整个客户端快照加密；不经过 server API
    cc::Bytes previousTag;
    std::string sourceDigest; // 续传时确认源文件没变
    bool outgoing = false;
    bool metadataQueued = false;
    bool metadataAcknowledged = false;
    bool complete = false;
    bool cancelled = false;
    std::filesystem::path finalPath;
    std::string name;
    std::uint32_t chunkCount = 0;
    std::optional<WireEnvelope> lastAck; // 最近累计签收的原密文，可在重复块到达时重发
    std::string partialDigest; // 校验恢复时 .part 的已确认前缀
    std::uint32_t transportVersion = 3; // 旧检查点只读，不能再续传
    std::vector<PendingFileChunk> pendingChunks; // 连续、最多 16 块，重试复用原密文
    std::optional<DirectFileEndpoint> directEndpoint;
    std::uint8_t path = 0; // 0=relay, 1=trying direct, 2=direct, 3=relay fallback
    std::int64_t timestamp = 0; // File send time in Unix ms; 0 for legacy records with no recorded time.
};
struct ReceivedEnvelope {
    std::string messageId;
    std::string from;
    std::string to;
    std::string kind;
    std::int64_t groupId = 0;
    std::string ciphertextSha256;
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
};
struct SendText {
    std::string recipient;
    std::string text;
    std::int64_t groupId = 0;
    std::string logicalGroupMessageId;
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
};
} // namespace ccfw
