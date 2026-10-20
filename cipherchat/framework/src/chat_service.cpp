#include "ccfw/chat_service.hpp"
#include "wire_helpers.hpp"
#include "feature_codec.hpp"
#include <algorithm>
namespace ccfw {
namespace {
Error chatError(ErrorCode code, const char* message) { return {code, "M4-01", message}; }
ChatRecord record(const PlainEnvelope& p, DeliveryState state) {
    return {p.messageId, p.logicalGroupMessageId, p.from, p.to, p.plaintext, p.groupId, p.timestamp, state, p.extensions};
}
}
Result<std::string> ChatService::sendText(const SendText& request) {
    if (request.recipient.empty() || request.recipient.size() > 32 || request.text.empty() || request.text.size() > 65536 || request.groupId < 0)
        return Result<std::string>::failure(chatError(ErrorCode::InvalidArgument, "收件人或消息文本非法"));
    auto loaded = store_.load(); if (!loaded) return Result<std::string>::failure(loaded.error());
    auto next = loaded.value();
    if (request.recipient == next.account.username) return Result<std::string>::failure(chatError(ErrorCode::InvalidState, "收件人无效"));
    if (request.groupId > 0 && !request.logicalGroupMessageId.empty()) {
        for (const auto& h : next.history) if (h.from == next.account.username && h.to == request.recipient && h.groupId == request.groupId &&
            h.logicalGroupMessageId == request.logicalGroupMessageId) {
            if (h.text != request.text) return Result<std::string>::failure(chatError(ErrorCode::Conflict, "群消息标识对应的正文已变化"));
            return Result<std::string>::success(h.messageId);
        }
    }
    try {
        (void)nlohmann::json(request.text).dump();
        if (!request.extensions.is_object() || request.extensions.dump().size() > 32768 || request.messageId.size() > 128)
            return Result<std::string>::failure(chatError(ErrorCode::InvalidArgument, "消息扩展或操作标识非法"));
        if (!request.messageId.empty()) for (const auto& h : next.history) if (h.messageId == request.messageId) {
            if (h.from != next.account.username || h.to != request.recipient || h.text != request.text || h.extensions != request.extensions)
                return Result<std::string>::failure(chatError(ErrorCode::Conflict, "消息操作标识已绑定其他内容"));
            return Result<std::string>::success(h.messageId);
        }
        PlainEnvelope plain{request.messageId.empty() ? wire::id() : request.messageId, request.logicalGroupMessageId, next.account.username, request.recipient, "chat", request.text, request.groupId, wire::now(), request.extensions};
        observe(plain.messageId, MessageStage::LocalQueued);
        auto packet=sessions_.encryptAndCommit(plain);
        if (!packet) { observe(plain.messageId,MessageStage::Failed); return Result<std::string>::failure(packet.error()); }
        auto sent=net_.send(wire::send(packet.value()));
        observe(plain.messageId,sent ? MessageStage::TransportQueued : MessageStage::Failed);
        events_.push_back(record(plain, DeliveryState::Queued));
        return Result<std::string>::success(plain.messageId);
    } catch (const std::exception&) { return Result<std::string>::failure(chatError(ErrorCode::InvalidArgument, "无法编码聊天文本")); }
}
Result<PeerIdentity> ChatService::offeredIdentity(const std::string&) const { return Result<PeerIdentity>::failure(chatError(ErrorCode::NotImplemented,"普通聊天不使用身份指纹")); }
Result<> ChatService::handleBundle(const cc::proto::PrekeyBundle&) { return Result<>::failure(chatError(ErrorCode::NotImplemented,"普通聊天不使用公钥束")); }
Result<> ChatService::retryPending(const std::string& peer) {
    auto loaded=store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    for (const auto& plain : loaded.value().pendingSends) if (plain.to==peer && wire::currentFileMessage(loaded.value(),plain.kind,plain.messageId)) {
        auto packet=sessions_.encryptAndCommit(plain); if (!packet) return Result<>::failure(packet.error());
        auto sent=net_.send(wire::send(packet.value())); if (!sent) return sent;
    }
    return Result<>::success({});
}
Result<ChatRecord> ChatService::receive(const WireEnvelope& message) {
    auto plain = sessions_.decryptAndCommit(message);
    if (!plain) {
        if (plain.error().code == ErrorCode::AlreadyProcessed) (void)net_.send(cc::proto::makeRecipientAck(message.messageId,message.from));
        return Result<ChatRecord>::failure(plain.error());
    }
    if (plain.value().kind != "chat") return Result<ChatRecord>::failure(chatError(ErrorCode::ProtocolError, "非文本消息路由错误"));
    (void)net_.send(cc::proto::makeRecipientAck(message.messageId, message.from));
    auto result = record(plain.value(), DeliveryState::RecipientReceived);
    observe(message.messageId, MessageStage::RecipientReceived);
    events_.push_back(result);
    return Result<ChatRecord>::success(std::move(result));
}
Result<> ChatService::handleAck(const std::string& id, DeliveryState state, const std::string& peer) {
    if (state != DeliveryState::ServerAccepted && state != DeliveryState::RecipientReceived) return Result<>::failure(chatError(ErrorCode::ProtocolError, "签收状态非法"));
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    auto next = loaded.value();
    auto out = std::find_if(next.outbox.begin(), next.outbox.end(), [&](const auto& p) { return p.messageId == id; });
    auto history = std::find_if(next.history.begin(), next.history.end(), [&](const auto& p) { return p.messageId == id && p.from == next.account.username; });
    if (out == next.outbox.end()) {
        // File transport receipts only acknowledge durable relay delivery; authenticated file_ack advances bytes.
        for (const auto& t : next.transfers) if (peer.empty() || t.peer == peer) {
            if ((t.outgoing && id == "fmeta:" + t.fileId) || std::any_of(t.pendingChunks.begin(), t.pendingChunks.end(), [&](const auto& pending) { return pending.envelope.messageId == id; }) ||
                (t.lastAck && t.lastAck->messageId == id)) return Result<>::success({});
            const std::string prefix = (t.outgoing ? "fchunk:" : "fack:") + t.fileId + ":";
            if (id.starts_with(prefix)) {
                const auto tail = id.substr(prefix.size());
                if (!tail.empty() && tail.size() <= 10 && std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                    const auto sequence = std::stoull(tail);
                    if (sequence < t.nextContiguousChunk || (!t.outgoing && sequence == UINT32_MAX)) return Result<>::success({});
                }
            }
        }
        if (history != next.history.end() && history->state == DeliveryState::RecipientReceived) return Result<>::success({});
        return Result<>::failure(chatError(ErrorCode::NotFound, "签收消息不存在"));
    }
    if (!peer.empty() && out->to != peer) return Result<>::failure(chatError(ErrorCode::ProtocolError, "签收对端不匹配"));
    std::optional<ChatRecord> changed;
    if (history != next.history.end() && static_cast<int>(history->state) < static_cast<int>(state)) {
        history->state = state; changed = *history;
    }
    if (state == DeliveryState::RecipientReceived) next.outbox.erase(out);
    else if (!changed) return Result<>::success({});
    const auto expected = next.revision++;
    auto saved = store_.commit(expected, next); if (!saved) return saved;
    if (changed) events_.push_back(*changed);
    if (changed) observe(id, state == DeliveryState::RecipientReceived ? MessageStage::RecipientReceived : MessageStage::ServerAccepted);
    return Result<>::success({});
}
Result<> ChatService::flushOutbox() {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    std::size_t count = 0;
    for (const auto& message : loaded.value().outbox) {
        if (!wire::currentFileMessage(loaded.value(), message.kind, message.messageId)) continue;
        auto sent = net_.send(wire::send(message));
        if (message.kind == "chat") observe(message.messageId, sent ? MessageStage::TransportQueued : MessageStage::Failed, sent ? -1 : static_cast<int>(sent.error().code), true);
        if (!sent) return sent;
        if (++count == 32) break;
    }
    std::vector<std::string> requested;
    for (const auto& pending : loaded.value().pendingSends) {
        if (!wire::currentFileMessage(loaded.value(), pending.kind, pending.messageId)) continue;
        if (std::find(requested.begin(), requested.end(), pending.to) != requested.end()) continue;
        requested.push_back(pending.to);
        auto sent = retryPending(pending.to); if (!sent) return sent;
        if (requested.size() == 16) break;
    }
    return Result<>::success({});
}
std::vector<ChatRecord> ChatService::takeEvents() { auto result = std::move(events_); events_.clear(); return result; }
void ChatService::observe(const std::string& id, MessageStage stage, int code, bool retry) {
    const auto now = std::chrono::steady_clock::now();
    auto it = traces_.find(id);
    if (it == traces_.end()) {
        if (traceOrder_.size() == 500) { traces_.erase(traceOrder_.front()); traceOrder_.pop_front(); }
        traceOrder_.push_back(id); it = traces_.emplace(id, Trace{now, 0, {}}).first;
    }
    auto& t = it->second; if (retry) ++t.retries;
    MessageStageEvent e{id, stage, wire::now(), std::chrono::duration_cast<std::chrono::milliseconds>(now - t.start).count(), t.retries, code};
    if (t.events.size() == 32) t.events.erase(t.events.begin());
    t.events.push_back(e);
    if (stageEvents_.size() == 1024) stageEvents_.erase(stageEvents_.begin());
    stageEvents_.push_back(e);
}
std::vector<MessageStageEvent> ChatService::takeStageEvents() { auto events = std::move(stageEvents_); stageEvents_.clear(); return events; }
std::vector<MessageStageEvent> ChatService::stages() const {
    std::vector<MessageStageEvent> result;
    for (const auto& id : traceOrder_) { const auto& events = traces_.at(id).events; result.insert(result.end(), events.begin(), events.end()); }
    return result;
}
}
