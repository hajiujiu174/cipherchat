#include "ccfw/session_service.hpp"
#include "wire_helpers.hpp"
#include "feature_codec.hpp"
#include <algorithm>

namespace ccfw {
namespace classic = nextalk::chat;
// Base64 is only framing for JSON/binary bytes, with transport protection provided by TLS.
// 按 ordinary-v1 编码普通消息信封，用同一事务保存待发送记录和文本历史。
Result<WireEnvelope> SessionService::encryptAndCommit(const PlainEnvelope& plain) {
    auto loaded=store_.load(); if(!loaded)return Result<WireEnvelope>::failure(loaded.error());
    auto next=loaded.value();
    // 构造当前分支的失败结果或拒绝响应，供调用处统一返回。
    auto fail=[](ErrorCode code){return Result<WireEnvelope>::failure({code,"ordinary-chat","消息字段或标识无效"});};
    if (!classic::validOutgoingRoute(next.account.username, plain.from, plain.to,
            plain.messageId, plain.kind, plain.groupId, plain.timestamp, plain.plaintext.size()))
        return fail(ErrorCode::InvalidArgument);
    if (classic::findMessageId(next.outbox.begin(), next.outbox.end(), plain.messageId) != next.outbox.end())
        return fail(ErrorCode::Conflict);
    try {
        nlohmann::json body={{"chat_mode","ordinary-v1"},{"message_id",plain.messageId},{"logical_group_message_id",plain.logicalGroupMessageId},
            {"from",plain.from},{"to",plain.to},{"kind",plain.kind},{"group_id",plain.groupId},{"timestamp",plain.timestamp},
            {"body_base64",cc::base64Encode(plain.plaintext)}};
        if(!plain.extensions.empty()) {
            wire::require(plain.kind=="chat" && plain.extensions.is_object() && plain.extensions.dump().size()<=32768);
            body["extensions"]=plain.extensions; features::projectExtension(next,plain);
        }
        WireEnvelope packet{plain.messageId,plain.from,plain.to,plain.kind,cc::base64Encode(body.dump()),plain.groupId,plain.timestamp};
        next.outbox.push_back(packet);
        if(plain.kind=="chat" && classic::findMessageId(next.history.begin(), next.history.end(), plain.messageId) == next.history.end())
            next.history.push_back({plain.messageId,plain.logicalGroupMessageId,plain.from,plain.to,plain.plaintext,plain.groupId,plain.timestamp,DeliveryState::Queued,plain.extensions});
        classic::clearQueuedMessage(next.pendingSends, plain.messageId);
        const auto revision=next.revision++; auto saved=store_.commit(revision,next); if(!saved)return Result<WireEnvelope>::failure(saved.error());
        return Result<WireEnvelope>::success(std::move(packet));
    }catch(const std::exception&){return fail(ErrorCode::ProtocolError);}
}
// 解析普通消息信封，核对内外层路由字段及重复记录，再保存收到的内容。
Result<PlainEnvelope> SessionService::decryptAndCommit(const WireEnvelope& packet) {
    // 构造当前分支的失败结果或拒绝响应，供调用处统一返回。
    auto fail=[](ErrorCode code){return Result<PlainEnvelope>::failure({code,"ordinary-chat","消息格式或收发路由不匹配，请确认双方均为新版客户端"});};
    auto loaded=store_.load(); if(!loaded)return Result<PlainEnvelope>::failure(loaded.error()); auto next=loaded.value();
    if (!classic::validIncomingRoute(packet, next.account.username)) return fail(ErrorCode::ProtocolError);
    const auto digest = cc::base64Encode(cc::crypto::sha256(packet.payloadBase64));
    const auto replay = classic::inspectReplay(next.receivedMessages, packet, digest);
    if (replay == classic::SameMessage) return fail(ErrorCode::AlreadyProcessed);
    if (replay == classic::MessageConflict) return fail(ErrorCode::Conflict);
    try {
        cc::Bytes raw; wire::require(cc::base64Decode(packet.payloadBase64,raw)); const auto body=nlohmann::json::parse(raw);
        wire::require(body.value("chat_mode",std::string{})=="ordinary-v1");
        PlainEnvelope plain{wire::text(body,"message_id",128),wire::text(body,"logical_group_message_id",128,true),
            wire::text(body,"from",32),wire::text(body,"to",32),wire::text(body,"kind",32),wire::bytes(body,"body_base64",1024*1024,true),
            wire::integer(body,"group_id"),wire::integer(body,"timestamp")};
        wire::require(classic::matchesRoute(packet, plain.messageId, plain.from, plain.to, plain.kind, plain.groupId));
        if(body.contains("extensions")){plain.extensions=body.at("extensions");features::projectExtension(next,plain);}
        classic::rememberReceipt(next.receivedMessages, packet, digest);
        if(plain.kind=="chat")next.history.push_back({plain.messageId,plain.logicalGroupMessageId,plain.from,plain.to,plain.plaintext,plain.groupId,plain.timestamp,DeliveryState::RecipientReceived,plain.extensions});
        else next.pendingPayloads.push_back(plain);
        const auto revision=next.revision++;auto saved=store_.commit(revision,next);if(!saved)return Result<PlainEnvelope>::failure(saved.error());
        return Result<PlainEnvelope>::success(std::move(plain));
    }catch(const std::exception&){return fail(ErrorCode::ProtocolError);}
}
}
