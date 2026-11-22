#include "ccfw/chat_service.hpp"
#include "message_flow.hpp"
#include "wire_helpers.hpp"
#include "feature_codec.hpp"
#include <algorithm>
#include <functional>
namespace ccfw {
namespace {
namespace chatFlow = nextalk::chat;
// 返回chat_service 模块使用的聊天错误。
Error chatError(ErrorCode code, const char* message) { return {code, "M4-01", message}; }
// 记录chat_service 模块。
ChatRecord record(const PlainEnvelope& p, DeliveryState state) {
    return {p.messageId, p.logicalGroupMessageId, p.from, p.to, p.plaintext, p.groupId, p.timestamp, state, p.extensions};
}

// 将 C++98 发送流程连接到现有的本地事务与 TLS 传输。
class OutgoingText final : public chatFlow::OutgoingText {
public:
    OutgoingText(ISessionService& session, ITransport& transport, const PlainEnvelope& message)
        : session_(session), transport_(transport), message_(message) {}

    bool saveToOutbox() override {
        auto saved = session_.encryptAndCommit(message_);
        if (!saved) {
            error_ = saved.error();
            return false;
        }
        packet_ = std::move(saved.value());
        return true;
    }

    bool submitSavedPacket() override {
        return transport_.send(wire::send(packet_)).ok();
    }

    const Error& error() const { return error_; }

private:
    ISessionService& session_;
    ITransport& transport_;
    const PlainEnvelope& message_;
    WireEnvelope packet_;
    Error error_{};
};

// 保存历史后把结果交给 C++98 流程，由它决定能否签收。
class IncomingText final : public chatFlow::IncomingText {
public:
    IncomingText(ISessionService& session, ITransport& transport, const WireEnvelope& packet)
        : session_(session), transport_(transport), packet_(packet) {}

    chatFlow::ReceiveOutcome saveToHistory() override {
        auto saved = session_.decryptAndCommit(packet_);
        if (!saved) {
            error_ = saved.error();
            return error_.code == ErrorCode::AlreadyProcessed ? chatFlow::DuplicateText : chatFlow::ReceiveFailed;
        }
        if (saved.value().kind != "chat") {
            error_ = chatError(ErrorCode::ProtocolError, "非文本消息路由错误");
            return chatFlow::NonText;
        }
        message_ = std::move(saved.value());
        return chatFlow::TextSaved;
    }

    void acknowledge() override {
        (void)transport_.send(cc::proto::makeRecipientAck(packet_.messageId, packet_.from));
    }

    const PlainEnvelope& message() const { return message_; }
    const Error& error() const { return error_; }

private:
    ISessionService& session_;
    ITransport& transport_;
    const WireEnvelope& packet_;
    PlainEnvelope message_;
    Error error_{};
};
}
// 校验消息内容和标识，将消息和发件箱持久化后再尝试网络发送。
Result<std::string> ChatService::sendText(const SendText& request) {
    if (!chatFlow::validTextRequest(request.recipient, request.text, request.groupId >= 0))
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
        OutgoingText delivery(sessions_, net_, plain);
        const auto outcome = chatFlow::sendText(delivery);
        if (outcome == chatFlow::SaveFailed) {
            observe(plain.messageId, MessageStage::Failed);
            return Result<std::string>::failure(delivery.error());
        }
        observe(plain.messageId, outcome == chatFlow::TransportQueued ? MessageStage::TransportQueued : MessageStage::Failed);
        events_.push_back(record(plain, DeliveryState::Queued));
        return Result<std::string>::success(plain.messageId);
    } catch (const std::exception&) { return Result<std::string>::failure(chatError(ErrorCode::InvalidArgument, "无法编码聊天文本")); }
}
// 重试指定对端仍在待发队列中的有效消息。
Result<> ChatService::retryPending(const std::string& peer) {
    auto loaded=store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    for (const auto& plain : loaded.value().pendingSends) if (plain.to==peer && wire::currentFileMessage(loaded.value(),plain.kind,plain.messageId)) {
        auto packet=sessions_.encryptAndCommit(plain); if (!packet) return Result<>::failure(packet.error());
        auto sent=net_.send(wire::send(packet.value())); if (!sent) return sent;
    }
    return Result<>::success({});
}
// 校验入站聊天消息并保存本地历史，落盘成功后才向对端返回签收。
Result<ChatRecord> ChatService::receive(const WireEnvelope& message) {
    IncomingText delivery(sessions_, net_, message);
    if (chatFlow::receiveText(delivery) != chatFlow::TextSaved)
        return Result<ChatRecord>::failure(delivery.error());
    auto result = record(delivery.message(), DeliveryState::RecipientReceived);
    observe(message.messageId, MessageStage::RecipientReceived);
    events_.push_back(result);
    return Result<ChatRecord>::success(std::move(result));
}
// 按服务端受理或收件方签收更新本机记录，收件方签收后清除对应待发项。
Result<> ChatService::handleAck(const std::string& id, DeliveryState state, const std::string& peer) {
    if (state != DeliveryState::ServerAccepted && state != DeliveryState::RecipientReceived) return Result<>::failure(chatError(ErrorCode::ProtocolError, "签收状态非法"));
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    auto next = loaded.value();
    auto out = chatFlow::findMessageId(next.outbox.begin(), next.outbox.end(), id);
    auto history = std::find_if(next.history.begin(), next.history.end(), [&](const auto& p) { return p.messageId == id && p.from == next.account.username; });
    if (out == next.outbox.end()) {
        for (const auto& transfer : next.transfers) {
            if ((!peer.empty() && transfer.peer != peer) ||
                !chatFlow::knownFileReceipt(transfer.outgoing, id, transfer.fileId,
                    transfer.nextContiguousChunk, transfer.pendingChunks,
                    transfer.lastAck ? &*transfer.lastAck : nullptr)) continue;
            return Result<>::success({});
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
// 从持久化发件箱取出尚未完成的消息，在连接可用时批量重发。
Result<> ChatService::flushOutbox() {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    struct Replay final : chatFlow::OutboxFlush {
        const ClientSnapshot& snapshot;
        std::function<bool(std::size_t)> sender;
        std::function<bool(const std::string&)> retryPeer;
        Replay(const ClientSnapshot& value, decltype(sender) send, decltype(retryPeer) retry)
            : snapshot(value), sender(std::move(send)), retryPeer(std::move(retry)) {}
        std::size_t queued() const override { return snapshot.outbox.size(); }
        std::size_t pending() const override { return snapshot.pendingSends.size(); }
        bool eligible(std::size_t index) const override {
            const auto& packet = snapshot.outbox[index];
            return wire::currentFileMessage(snapshot, packet.kind, packet.messageId);
        }
        bool pendingPeer(std::size_t index, std::string& peer) const override {
            const auto& message = snapshot.pendingSends[index];
            if (!wire::currentFileMessage(snapshot, message.kind, message.messageId)) return false;
            peer = message.to;
            return true;
        }
        bool submit(std::size_t index) override { return sender(index); }
        bool retry(const std::string& peer) override { return retryPeer(peer); }
    };
    auto result = Result<>::success({});
    const auto& snapshot = loaded.value();
    Replay replay(snapshot, [&](std::size_t index) {
        const auto& message = snapshot.outbox[index];
        result = net_.send(wire::send(message));
        if (message.kind == "chat")
            observe(message.messageId, result ? MessageStage::TransportQueued : MessageStage::Failed,
                    result ? -1 : static_cast<int>(result.error().code), true);
        return result.ok();
    }, [&](const std::string& peer) {
        result = retryPending(peer);
        return result.ok();
    });
    (void)chatFlow::flushOutbox(replay);
    return result;
}
// 取出事件。
std::vector<ChatRecord> ChatService::takeEvents() { auto result = std::move(events_); events_.clear(); return result; }
// 记录聊天服务。
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
// 取出阶段事件。
std::vector<MessageStageEvent> ChatService::takeStageEvents() { auto events = std::move(stageEvents_); stageEvents_.clear(); return events; }
// 返回聊天服务使用的状态阶段。
std::vector<MessageStageEvent> ChatService::stages() const {
    std::vector<MessageStageEvent> result;
    for (const auto& id : traceOrder_) { const auto& events = traces_.at(id).events; result.insert(result.end(), events.begin(), events.end()); }
    return result;
}
}
