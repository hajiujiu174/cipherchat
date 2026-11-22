#include "ccfw/session_service.hpp"
#include "wire_helpers.hpp"
#include "feature_codec.hpp"
#include <algorithm>

namespace ccfw {
Result<> SessionService::acceptPeerIdentity(const PeerIdentity&) {
    return Result<>::failure({ErrorCode::NotImplemented,"ordinary-chat","普通聊天不使用身份指纹"});
}
Result<> SessionService::establish(const std::string&,const cc::ratchet::X3dhPeerBundle&) {
    return Result<>::failure({ErrorCode::NotImplemented,"ordinary-chat","普通聊天不建立加密会话"});
}
// Base64 is only framing for JSON/binary bytes, with transport protection provided by TLS.
Result<WireEnvelope> SessionService::encryptAndCommit(const PlainEnvelope& plain) {
    auto loaded=store_.load(); if(!loaded)return Result<WireEnvelope>::failure(loaded.error());
    auto next=loaded.value();
    auto fail=[](ErrorCode code){return Result<WireEnvelope>::failure({code,"ordinary-chat","消息字段或标识无效"});};
    if(plain.from!=next.account.username || plain.to==plain.from || plain.to.empty() || plain.to.size()>32 ||
       plain.messageId.empty() || plain.messageId.size()>128 || plain.groupId<0 || plain.timestamp<0 ||
       !wire::kind(plain.kind) || plain.plaintext.size()>1024*1024)return fail(ErrorCode::InvalidArgument);
    if(std::any_of(next.outbox.begin(),next.outbox.end(),[&](const auto& p){return p.messageId==plain.messageId;}))return fail(ErrorCode::Conflict);
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
        if(plain.kind=="chat" && std::none_of(next.history.begin(),next.history.end(),[&](const auto& h){return h.messageId==plain.messageId;}))
            next.history.push_back({plain.messageId,plain.logicalGroupMessageId,plain.from,plain.to,plain.plaintext,plain.groupId,plain.timestamp,DeliveryState::Queued,plain.extensions});
        std::erase_if(next.pendingSends,[&](const auto& p){return p.messageId==plain.messageId;});
        const auto revision=next.revision++; auto saved=store_.commit(revision,next); if(!saved)return Result<WireEnvelope>::failure(saved.error());
        return Result<WireEnvelope>::success(std::move(packet));
    }catch(const std::exception&){return fail(ErrorCode::ProtocolError);}
}
Result<PlainEnvelope> SessionService::decryptAndCommit(const WireEnvelope& packet) {
    auto fail=[](ErrorCode code){return Result<PlainEnvelope>::failure({code,"ordinary-chat","消息格式或收发路由不匹配，请确认双方均为新版客户端"});};
    auto loaded=store_.load(); if(!loaded)return Result<PlainEnvelope>::failure(loaded.error()); auto next=loaded.value();
    if(packet.to!=next.account.username || packet.from==packet.to || packet.from.empty() || packet.from.size()>32 ||
       packet.messageId.empty() || packet.messageId.size()>128 || packet.groupId<0 || !wire::kind(packet.kind) || packet.payloadBase64.size()>3*1024*1024)return fail(ErrorCode::ProtocolError);
    const auto digest=cc::base64Encode(cc::crypto::sha256(packet.payloadBase64));
    for(const auto& prior:next.receivedMessages)if(prior.messageId==packet.messageId) {
        if(prior.from==packet.from && prior.to==packet.to && prior.kind==packet.kind && prior.groupId==packet.groupId && prior.ciphertextSha256==digest)return fail(ErrorCode::AlreadyProcessed);
        return fail(ErrorCode::Conflict);
    }
    try {
        cc::Bytes raw; wire::require(cc::base64Decode(packet.payloadBase64,raw)); const auto body=nlohmann::json::parse(raw);
        wire::require(body.value("chat_mode",std::string{})=="ordinary-v1");
        PlainEnvelope plain{wire::text(body,"message_id",128),wire::text(body,"logical_group_message_id",128,true),
            wire::text(body,"from",32),wire::text(body,"to",32),wire::text(body,"kind",32),wire::bytes(body,"body_base64",1024*1024,true),
            wire::integer(body,"group_id"),wire::integer(body,"timestamp")};
        wire::require(plain.messageId==packet.messageId && plain.from==packet.from && plain.to==packet.to && plain.kind==packet.kind && plain.groupId==packet.groupId);
        if(body.contains("extensions")){plain.extensions=body.at("extensions");features::projectExtension(next,plain);}
        next.receivedMessages.push_back({plain.messageId,plain.from,plain.to,plain.kind,plain.groupId,digest});
        if(next.receivedMessages.size()>4096)next.receivedMessages.erase(next.receivedMessages.begin());
        if(plain.kind=="chat")next.history.push_back({plain.messageId,plain.logicalGroupMessageId,plain.from,plain.to,plain.plaintext,plain.groupId,plain.timestamp,DeliveryState::RecipientReceived,plain.extensions});
        else next.pendingPayloads.push_back(plain);
        const auto revision=next.revision++;auto saved=store_.commit(revision,next);if(!saved)return Result<PlainEnvelope>::failure(saved.error());
        return Result<PlainEnvelope>::success(std::move(plain));
    }catch(const std::exception&){return fail(ErrorCode::ProtocolError);}
}
}
