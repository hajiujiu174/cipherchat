// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_MESSAGE_RECORDS_HPP
#define NEXTALK_CHAT_MESSAGE_RECORDS_HPP

#include <stdint.h>
#include <string>
#include <vector>

namespace ccfw {

// 协议层和本地快照共用同一套字段，原 JSON 序列化仍由应用层处理。
struct WireEnvelope {
    std::string messageId;
    std::string from;
    std::string to;
    std::string kind;
    std::string payloadBase64;
    int64_t groupId;
    int64_t timestamp;

    WireEnvelope() : kind("chat"), groupId(0), timestamp(0) {}
    WireEnvelope(const std::string& id, const std::string& sender,
                 const std::string& recipient, const std::string& type,
                 const std::string& bytes, int64_t group = 0, int64_t time = 0)
        : messageId(id), from(sender), to(recipient), kind(type), payloadBase64(bytes),
          groupId(group), timestamp(time) {}
};

struct ReceivedEnvelope {
    std::string messageId;
    std::string from;
    std::string to;
    std::string kind;
    int64_t groupId;
    std::string ciphertextSha256;

    ReceivedEnvelope() : groupId(0) {}
    ReceivedEnvelope(const std::string& id, const std::string& sender,
                     const std::string& recipient, const std::string& type,
                     int64_t group, const std::string& digest)
        : messageId(id), from(sender), to(recipient), kind(type), groupId(group),
          ciphertextSha256(digest) {}
};

struct PendingFileChunk {
    WireEnvelope envelope;
    std::string tag;
    PendingFileChunk() {}
    PendingFileChunk(const WireEnvelope& packet, const std::string& checksum)
        : envelope(packet), tag(checksum) {}
};

} // namespace ccfw

namespace nextalk {
namespace chat {

template<class Iterator>
Iterator findMessageId(Iterator first, Iterator last, const std::string& id) {
    for (; first != last; ++first) {
        if (first->messageId == id) break;
    }
    return first;
}

template<class Message>
void clearQueuedMessage(std::vector<Message>& queued, const std::string& id) {
    typename std::vector<Message>::iterator out = queued.begin();
    for (typename std::vector<Message>::iterator item = queued.begin(); item != queued.end(); ++item) {
        if (item->messageId == id) continue;
        if (out != item) *out = *item;
        ++out;
    }
    queued.erase(out, queued.end());
}

enum ReplayStatus { NewMessage, SameMessage, MessageConflict };

bool validMessageId(const std::string& id);
bool supportedSessionKind(const std::string& kind);
bool validOutgoingRoute(const std::string& owner, const std::string& from, const std::string& to,
                        const std::string& id, const std::string& kind,
                        int64_t group, int64_t stamp, std::size_t size);
bool validIncomingRoute(const ccfw::WireEnvelope& packet, const std::string& owner);
bool matchesRoute(const ccfw::WireEnvelope& packet, const std::string& id, const std::string& from,
                  const std::string& to, const std::string& kind, int64_t group);
ReplayStatus inspectReplay(const std::vector<ccfw::ReceivedEnvelope>& receipts,
                           const ccfw::WireEnvelope& packet, const std::string& digest);
void rememberReceipt(std::vector<ccfw::ReceivedEnvelope>& receipts,
                     const ccfw::WireEnvelope& packet, const std::string& digest);

bool currentFileMessage(const std::string& kind, const std::string& id,
                        const std::string& fileId, bool currentProtocol);
bool knownFileReceipt(bool outgoing, const std::string& id, const std::string& fileId,
                      uint32_t contiguous, const std::vector<ccfw::PendingFileChunk>& pending,
                      const ccfw::WireEnvelope* lastAck);

class OutboxFlush {
public:
    virtual ~OutboxFlush() {}
    virtual std::size_t queued() const = 0;
    virtual bool eligible(std::size_t index) const = 0;
    virtual bool submit(std::size_t index) = 0;
    virtual std::size_t pending() const = 0;
    virtual bool pendingPeer(std::size_t index, std::string& peer) const = 0;
    virtual bool retry(const std::string& peer) = 0;
};

bool flushOutbox(OutboxFlush& flush);

} // namespace chat
} // namespace nextalk

#endif
