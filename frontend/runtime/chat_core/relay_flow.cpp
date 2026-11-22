// 人工实现，由 ChatGPT 规范化。

#include "relay_flow.hpp"
#include "binary.hpp"

namespace nextalk {
namespace chat {
namespace {

bool validRoute(const RelayRequest& request) {
    return !request.recipient.empty() && request.recipient != request.sender;
}

bool validRelayFields(const RelayRequest& request) {
    if (request.messageId.empty() || request.messageId.size() > 128 ||
        request.recipient.size() > 32 || !request.validGroupField || request.groupId < 0) {
        return false;
    }
    if (request.kind != "chat" && request.kind != "file_meta" &&
        request.kind != "file_chunk" && request.kind != "file_ack") {
        return false;
    }
    if (request.payload.empty() || request.payload.size() > 7 * 1024 * 1024) {
        return false;
    }
    std::string decoded;
    return base64Decode(request.payload.data(), request.payload.size(), decoded);
}

RelayReply saveRelay(const RelayRequest& request, RelaySubmitPort& port) {
    if (!validRoute(request)) {
        return RelayBadTarget;
    }
    if (!port.accountExists(request.recipient)) {
        return RelayNoUser;
    }
    if (!validRelayFields(request)) {
        return RelayBadFields;
    }
    if (request.groupId == 0 && port.isBlocked(request)) {
        return RelayBlocked;
    }
    if (request.groupId != 0 && (request.kind != "chat" || !port.groupAllows(request))) {
        return RelayForbiddenGroup;
    }
    switch (port.persist(request)) {
    case QueueOk:
        return RelayAccepted;
    case QueueAlreadyProcessed:
        return RelayAlreadyReceived;
    default:
        return RelayRejected;
    }
}

} // namespace

// 回执必须在存储结果确定后发出。同一消息已经签收时直接回复历史结果。
void submitRelay(const RelayRequest& request, RelaySubmitPort& port) {
    const RelayReply result = saveRelay(request, port);
    port.reply(request, result);
    if (result == RelayAccepted) {
        port.deliverOnline(request.recipient);
    }
}

// 按持久队列顺序补发。连接关闭会丢弃在途表，重连仍从 SQLite 读取待签收记录。
void pumpRelayQueue(const std::vector<PendingMessage>& pending,
                    InFlightMessages& inFlight, QueuedDeliveryPort& port) {
    const std::size_t maxMessages = 32;
    const std::size_t maxBytes = 8 * 1024 * 1024;
    std::size_t bytes = 0;
    for (InFlightMessages::const_iterator it = inFlight.begin(); it != inFlight.end(); ++it) {
        bytes += it->second;
    }
    for (std::vector<PendingMessage>::const_iterator it = pending.begin(); it != pending.end(); ++it) {
        if (inFlight.count(it->messageId)) continue;
        if (inFlight.size() >= maxMessages || bytes > maxBytes || it->payload.size() > maxBytes - bytes) {
            break;
        }
        port.deliverStored(*it);
        inFlight[it->messageId] = it->payload.size();
        bytes += it->payload.size();
    }
}

// 只接受属于当前接收者、且回执目标与原发件人相符的签收。
void receiveRelayAck(const std::string& id, const std::string& sender, RelayAckPort& port) {
    if (id.empty() || id.size() > 128 || sender.empty()) {
        port.rejectAck(RelayBadAck);
        return;
    }
    PendingMessage message;
    if (!port.findPending(id, message)) {
        return;
    }
    if (message.sender != sender) {
        port.rejectAck(RelayWrongSender);
        return;
    }
    port.persistAck(id);
    port.releaseInFlight(id);
    port.notifySender(message);
    port.continueDelivery();
}

} // namespace chat
} // namespace nextalk
