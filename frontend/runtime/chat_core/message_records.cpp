// 人工实现，由 ChatGPT 规范化。

#include "message_records.hpp"

#include <limits>
#include <set>

namespace nextalk {
namespace chat {
namespace {

const std::size_t kMaxBody = 1024 * 1024;
const std::size_t kMaxEncodedBody = 3 * kMaxBody;
const std::size_t kReceiptHistory = 4096;

bool hasPrefix(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool parseSequence(const std::string& value, uint64_t& sequence) {
    if (value.empty() || value.size() > 10) return false;
    uint64_t parsed = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        parsed = parsed * 10 + (value[i] - '0');
    }
    sequence = parsed;
    return true;
}
}

bool validMessageId(const std::string& id) {
    return !id.empty() && id.size() <= 128;
}

bool supportedSessionKind(const std::string& kind) {
    return kind == "chat" || kind == "file_meta" || kind == "file_ack";
}

bool validOutgoingRoute(const std::string& owner, const std::string& from, const std::string& to,
                        const std::string& id, const std::string& kind,
                        int64_t group, int64_t stamp, std::size_t size) {
    return from == owner && to != from && !to.empty() && to.size() <= 32 &&
           validMessageId(id) && group >= 0 && stamp >= 0 &&
           supportedSessionKind(kind) && size <= kMaxBody;
}

bool validIncomingRoute(const ccfw::WireEnvelope& packet, const std::string& owner) {
    return packet.to == owner && packet.from != packet.to && !packet.from.empty() &&
           packet.from.size() <= 32 && validMessageId(packet.messageId) &&
           packet.groupId >= 0 && supportedSessionKind(packet.kind) &&
           packet.payloadBase64.size() <= kMaxEncodedBody;
}

bool matchesRoute(const ccfw::WireEnvelope& packet, const std::string& id, const std::string& from,
                  const std::string& to, const std::string& kind, int64_t group) {
    return packet.messageId == id && packet.from == from && packet.to == to &&
           packet.kind == kind && packet.groupId == group;
}

ReplayStatus inspectReplay(const std::vector<ccfw::ReceivedEnvelope>& receipts,
                           const ccfw::WireEnvelope& packet, const std::string& digest) {
    const std::vector<ccfw::ReceivedEnvelope>::const_iterator entry =
        findMessageId(receipts.begin(), receipts.end(), packet.messageId);
    if (entry == receipts.end()) return NewMessage;
    const bool same = matchesRoute(packet, entry->messageId, entry->from,
                                   entry->to, entry->kind, entry->groupId) &&
                      entry->ciphertextSha256 == digest;
    return same ? SameMessage : MessageConflict;
}

void rememberReceipt(std::vector<ccfw::ReceivedEnvelope>& receipts,
                     const ccfw::WireEnvelope& packet, const std::string& digest) {
    receipts.push_back(ccfw::ReceivedEnvelope(packet.messageId, packet.from, packet.to,
                                               packet.kind, packet.groupId, digest));
    if (receipts.size() > kReceiptHistory) receipts.erase(receipts.begin());
}

bool currentFileMessage(const std::string& kind, const std::string& id,
                        const std::string& fileId, bool currentProtocol) {
    if (currentProtocol || (kind != "file_meta" && kind != "file_ack" && kind != "file_chunk"))
        return true;
    return id != "fmeta:" + fileId && !hasPrefix(id, "fack:" + fileId + ":") &&
           !hasPrefix(id, "fchunk:" + fileId + ":");
}

// 历史文件的网络层签收不推进字节数，累计字节只由文件层更新。
bool knownFileReceipt(bool outgoing, const std::string& id, const std::string& fileId,
                      uint32_t contiguous, const std::vector<ccfw::PendingFileChunk>& pending,
                      const ccfw::WireEnvelope* lastAck) {
    if ((outgoing && id == "fmeta:" + fileId) || (lastAck && lastAck->messageId == id))
        return true;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (pending[i].envelope.messageId == id) return true;
    }
    const std::string prefix = (outgoing ? "fchunk:" : "fack:") + fileId + ":";
    uint64_t sequence = 0;
    return hasPrefix(id, prefix) && parseSequence(id.substr(prefix.size()), sequence) &&
           (sequence < contiguous || (!outgoing && sequence == std::numeric_limits<uint32_t>::max()));
}

// 重连先重发已保存的原始信封，再按对端处理需要重新准备的任务。
bool flushOutbox(OutboxFlush& flush) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < flush.queued(); ++i) {
        if (!flush.eligible(i)) continue;
        if (!flush.submit(i)) return false;
        if (++count == 32) break;
    }
    std::set<std::string> requested;
    for (std::size_t i = 0; i < flush.pending(); ++i) {
        std::string peer;
        if (!flush.pendingPeer(i, peer) || !requested.insert(peer).second) continue;
        if (!flush.retry(peer)) return false;
        if (requested.size() == 16) break;
    }
    return true;
}

} // namespace chat
} // namespace nextalk
