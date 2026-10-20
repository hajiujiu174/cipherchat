#include "ccfw/chat_service.hpp"
#include "wire_helpers.hpp"
#include <algorithm>
namespace ccfw {
namespace {
Error chatError(ErrorCode code, const char* message) { return {code, "M4-01", message}; }
ChatRecord record(const PlainEnvelope& p, DeliveryState state) {
    return {p.messageId, p.logicalGroupMessageId, p.from, p.to, p.plaintext, p.groupId, p.timestamp, state};
}
}
Result<std::string> ChatService::sendText(const SendText& request) {
    if (request.recipient.empty() || request.recipient.size() > 32 || request.text.empty() || request.text.size() > 65536 || request.groupId < 0)
        return Result<std::string>::failure(chatError(ErrorCode::InvalidArgument, "收件人或消息文本非法"));
    auto loaded = store_.load(); if (!loaded) return Result<std::string>::failure(loaded.error());
    auto next = loaded.value();
    if (!next.identity || request.recipient == next.account.username) return Result<std::string>::failure(chatError(ErrorCode::InvalidState, "本地身份或收件人无效"));
    if (request.groupId > 0 && !request.logicalGroupMessageId.empty()) {
        for (const auto& h : next.history) if (h.from == next.account.username && h.to == request.recipient && h.groupId == request.groupId &&
            h.logicalGroupMessageId == request.logicalGroupMessageId) {
            if (h.text != request.text) return Result<std::string>::failure(chatError(ErrorCode::Conflict, "群消息标识对应的正文已变化"));
            return Result<std::string>::success(h.messageId);
        }
    }
    try {
        (void)nlohmann::json(request.text).dump();
        PlainEnvelope plain{wire::id(), request.logicalGroupMessageId, next.account.username, request.recipient, "chat", request.text, request.groupId, wire::now()};
        const bool hasSession = std::any_of(next.sessions.begin(), next.sessions.end(), [&](const auto& s) { return s.peer == plain.to; });
        if (hasSession) {
            auto encrypted = sessions_.encryptAndCommit(plain);
            if (!encrypted) return Result<std::string>::failure(encrypted.error());
            // 发送失败仍是已可靠排队；重连使用原 outbox 密文。
            (void)net_.send(wire::send(encrypted.value()));
        } else {
            if (std::count_if(next.pendingSends.begin(), next.pendingSends.end(), [&](const auto& p) { return p.to == plain.to; }) >= 64)
                return Result<std::string>::failure(chatError(ErrorCode::Forbidden, "该对端待建会话消息已达上限"));
            next.pendingSends.push_back(plain); next.history.push_back(record(plain, DeliveryState::Queued));
            const auto expected = next.revision++;
            auto saved = store_.commit(expected, next); if (!saved) return Result<std::string>::failure(saved.error());
            (void)net_.send(cc::proto::withV2(cc::proto::makeFetchBundle(plain.to)));
        }
        events_.push_back(record(plain, DeliveryState::Queued));
        return Result<std::string>::success(plain.messageId);
    } catch (const std::exception&) { return Result<std::string>::failure(chatError(ErrorCode::InvalidArgument, "无法编码聊天文本")); }
}
Result<PeerIdentity> ChatService::offeredIdentity(const std::string& peer) const {
    auto entry = bundles_.find(peer);
    if (entry == bundles_.end()) return Result<PeerIdentity>::failure(chatError(ErrorCode::NotFound, "尚未取得对端公钥"));
    try {
        const auto decoded = wire::bundle(entry->second);
        return Result<PeerIdentity>::success({peer, decoded.signPub, decoded.ikPub, false});
    } catch (const std::exception&) { return Result<PeerIdentity>::failure(chatError(ErrorCode::ProtocolError, "公钥束签名或格式无效")); }
}
Result<> ChatService::handleBundle(const cc::proto::PrekeyBundle& encoded) {
    try { (void)wire::bundle(encoded); }
    catch (const std::exception&) { return Result<>::failure(chatError(ErrorCode::AuthenticationFailed, "公钥束签名或格式无效")); }
    bundles_[encoded.username] = encoded;
    return retryPending(encoded.username);
}
Result<> ChatService::retryPending(const std::string& peer) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    auto offered = offeredIdentity(peer); if (!offered) return Result<>::failure(offered.error());
    auto trusted = std::find_if(loaded.value().trustedPeers.begin(), loaded.value().trustedPeers.end(), [&](const auto& p) { return p.username == peer; });
    if (trusted == loaded.value().trustedPeers.end() || trusted->dhPublicKey != offered.value().dhPublicKey || trusted->signingPublicKey != offered.value().signingPublicKey)
        return Result<>::failure(chatError(ErrorCode::IdentityChanged, "请核对并确认对端身份指纹"));
    auto received = std::move(incoming_[peer]); incoming_.erase(peer);
    for (const auto& message : received) {
        auto result = receive(message);
        if (!result && result.error().code != ErrorCode::AlreadyProcessed) return Result<>::failure(result.error());
    }
    const auto pending = loaded.value().pendingSends;
    const auto current = [&](const auto& p) { return p.to == peer && wire::currentFileMessage(loaded.value(), p.kind, p.messageId); };
    if (std::any_of(pending.begin(), pending.end(), current)) {
        auto established = sessions_.establish(peer, wire::bundle(bundles_.at(peer))); if (!established) return established;
        for (const auto& plain : pending) if (current(plain)) {
            auto encrypted = sessions_.encryptAndCommit(plain); if (!encrypted) return Result<>::failure(encrypted.error());
            auto sent = net_.send(wire::send(encrypted.value())); if (!sent) return sent;
        }
    }
    return Result<>::success({});
}
Result<ChatRecord> ChatService::receive(const WireEnvelope& message) {
    auto plain = sessions_.decryptAndCommit(message);
    if (!plain) {
        if (plain.error().code == ErrorCode::IdentityChanged) {
            auto& pending = incoming_[message.from];
            auto duplicate = std::find_if(pending.begin(), pending.end(), [&](const auto& p) { return p.messageId == message.messageId; });
            if (duplicate == pending.end()) {
                if (pending.size() >= 64) return Result<ChatRecord>::failure(chatError(ErrorCode::Forbidden, "待确认身份的收件队列已满"));
                pending.push_back(message);
            }
            if (pending.size() == 1) (void)net_.send(cc::proto::withV2(cc::proto::makeFetchBundle(message.from)));
        } else if (plain.error().code == ErrorCode::AlreadyProcessed) {
            (void)net_.send(cc::proto::makeRecipientAck(message.messageId, message.from));
        }
        return Result<ChatRecord>::failure(plain.error());
    }
    if (plain.value().kind != "chat") return Result<ChatRecord>::failure(chatError(ErrorCode::ProtocolError, "非文本消息路由错误"));
    (void)net_.send(cc::proto::makeRecipientAck(message.messageId, message.from));
    auto result = record(plain.value(), DeliveryState::RecipientReceived);
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
    return Result<>::success({});
}
Result<> ChatService::flushOutbox() {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    std::size_t count = 0;
    for (const auto& message : loaded.value().outbox) {
        if (!wire::currentFileMessage(loaded.value(), message.kind, message.messageId)) continue;
        auto sent = net_.send(wire::send(message)); if (!sent) return sent;
        if (++count == 32) break;
    }
    std::vector<std::string> requested;
    for (const auto& pending : loaded.value().pendingSends) {
        if (!wire::currentFileMessage(loaded.value(), pending.kind, pending.messageId)) continue;
        if (std::find(requested.begin(), requested.end(), pending.to) != requested.end()) continue;
        requested.push_back(pending.to);
        auto sent = net_.send(cc::proto::withV2(cc::proto::makeFetchBundle(pending.to))); if (!sent) return sent;
        if (requested.size() == 16) break;
    }
    return Result<>::success({});
}
std::vector<ChatRecord> ChatService::takeEvents() { auto result = std::move(events_); events_.clear(); return result; }
}
