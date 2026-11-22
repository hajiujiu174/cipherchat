#include "ccfw/session_service.hpp"
#include "wire_helpers.hpp"

#include <algorithm>
#include <stdexcept>

namespace ccfw {
namespace {

constexpr const char* kTask = "M1-03";

Error makeError(ErrorCode code, const std::string& message) {
    return Error{code, kTask, message};
}

// 会话绑定材料：双方身份 DH 公钥按字典序拼接（双端一致）
cc::Bytes buildAd(const cc::Bytes& own, const cc::Bytes& peer) {
    return own < peer ? own + peer : peer + own;
}

const PeerIdentity* findTrusted(const ClientSnapshot& snap,
                                const std::string& peer) {
    for (const auto& p : snap.trustedPeers) {
        if (p.username == peer) return &p;
    }
    return nullptr;
}

}  // namespace

// 首次接触或对端身份与信任记录不一致时返回 IdentityChanged；
// 只有显式的 acceptPeerIdentity 会写入/更新信任记录。
Result<> SessionService::acceptPeerIdentity(const PeerIdentity& identity) {
    auto snap = store_.load();
    if (!snap.ok()) {
        return Result<>::failure(
            makeError(ErrorCode::StorageFailure, "无法读取本地快照"));
    }
    ClientSnapshot next = snap.value();
    if (!next.identity.has_value()) {
        return Result<>::failure(
            makeError(ErrorCode::InvalidState, "本地身份尚未建立"));
    }
    if (identity.username.empty() || identity.username.size() > 32 || identity.signingPublicKey.size() != 32 ||
        identity.dhPublicKey.size() != 32) {
        return Result<>::failure(
            makeError(ErrorCode::InvalidArgument, "身份材料不完整"));
    }
    bool replaced = false;
    for (auto& p : next.trustedPeers) {
        if (p.username == identity.username) {
            if (p.signingPublicKey != identity.signingPublicKey || p.dhPublicKey != identity.dhPublicKey)
                std::erase_if(next.sessions, [&](const auto& session) { return session.peer == identity.username; });
            p = identity;  // 显式重新确认才允许覆盖
            replaced = true;
        }
    }
    if (!replaced) next.trustedPeers.push_back(identity);
    next.revision = snap.value().revision + 1;
    const auto committed = store_.commit(snap.value().revision, next);
    if (!committed.ok()) {
        return Result<>::failure(
            makeError(ErrorCode::StorageFailure, "信任记录提交失败"));
    }
    return Result<>::success(Unit{});
}

Result<> SessionService::establish(const std::string& peer,
                                   const cc::ratchet::X3dhPeerBundle& bundle) {
    if (peer.empty() || bundle.ikPub.size() != 32 || bundle.spkPub.size() != 32 ||
        bundle.signPub.size() != 32 || bundle.spkSig.size() != 64 ||
        (bundle.opkId && (*bundle.opkId <= 0 || bundle.opkPub.size() != 32)) || (!bundle.opkId && !bundle.opkPub.empty())) {
        return Result<>::failure(
            makeError(ErrorCode::InvalidArgument, "公钥束不完整"));
    }
    auto snap = store_.load();
    if (!snap.ok()) {
        return Result<>::failure(
            makeError(ErrorCode::StorageFailure, "无法读取本地快照"));
    }
    ClientSnapshot current = snap.value();
    if (!current.identity.has_value()) {
        return Result<>::failure(
            makeError(ErrorCode::InvalidState, "本地身份尚未建立"));
    }

    // 1) 核对用户名绑定的签名 / DH 身份公钥：未知或变更一律 IdentityChanged，
    //    绝不静默接受服务器转来的新公钥（防服务器替换公钥束）。
    const PeerIdentity* trusted = findTrusted(current, peer);
    if (!trusted) {
        return Result<>::failure(makeError(
            ErrorCode::IdentityChanged, "未知对端身份，等待显式确认"));
    }
    if (trusted->signingPublicKey != bundle.signPub ||
        trusted->dhPublicKey != bundle.ikPub) {
        return Result<>::failure(makeError(
            ErrorCode::IdentityChanged, "对端身份公钥与信任记录不一致"));
    }

    // 2) 校验 SPK 签名（对端长期签名公钥验证，防束被篡改）
    if (!cc::crypto::ed25519Verify(bundle.signPub, bundle.spkPub,
                                   bundle.spkSig)) {
        return Result<>::failure(
            makeError(ErrorCode::AuthenticationFailed, "预共享密钥签名无效"));
    }

    for (const auto& session : current.sessions) if (session.peer == peer) return Result<>::success({});
    // 3) 复用既有 X3DH 与双棘轮（不重写算法）
    const auto& identity = current.identity.value();
    const cc::Bytes ownIkPub = identity.identityDh.pub;
    cc::ratchet::X3dhInitiation init;
    try {
        init = cc::ratchet::x3dhInitiate(identity.identityDh.priv, ownIkPub,
                                         bundle);
    } catch (const std::exception&) {
        return Result<>::failure(
            makeError(ErrorCode::CryptoFailure, "X3DH 初始化失败"));
    }

    cc::ratchet::DoubleRatchet ratchet =
        cc::ratchet::DoubleRatchet::initAsInitiator(init.rootKey,
                                                    bundle.spkPub);

    // 4) 原子提交：会话记录（含棘轮状态与 AD），revision 严格 +1
    SessionRecord record;
    record.peer = peer;
    record.sessionId = cc::base64Encode(cc::crypto::sha256(init.ikPub + init.ekPub + bundle.ikPub));
    record.associatedData = buildAd(ownIkPub, bundle.ikPub);
    record.ratchetState = ratchet.toJson();
    record.pendingPrekey = cc::ratchet::Header{};
    record.pendingPrekey->ikPub = init.ikPub;
    record.pendingPrekey->ekPub = init.ekPub;
    record.pendingPrekey->opkId = init.opkId;

    ClientSnapshot next = current;
    bool replaced = false;
    for (auto& s : next.sessions) {
        if (s.peer == peer) {
            s = record;  // 重建会话：覆盖旧记录（旧状态由 revision 链保留）
            replaced = true;
        }
    }
    if (!replaced) next.sessions.push_back(record);
    next.revision = current.revision + 1;
    const auto committed = store_.commit(current.revision, next);
    if (!committed.ok()) {
        return Result<>::failure(
            makeError(ErrorCode::StorageFailure, "会话提交失败"));
    }
    return Result<>::success(Unit{});
}

// encryptAndCommit / decryptAndCommit 属于 M4-01（可靠发件箱与原子收件）。
Result<WireEnvelope> SessionService::encryptAndCommit(const PlainEnvelope& plain) {
    auto loaded = store_.load();
    if (!loaded) return Result<WireEnvelope>::failure(loaded.error());
    auto next = loaded.value();
    if (!next.identity || plain.from != next.account.username || plain.to == plain.from ||
        plain.to.empty() || plain.messageId.empty() || plain.messageId.size() > 128 || plain.groupId < 0 ||
        plain.timestamp < 0 || !wire::kind(plain.kind) || plain.plaintext.size() > 1024 * 1024)
        return Result<WireEnvelope>::failure({ErrorCode::InvalidArgument, "M4-01", "待加密消息字段非法"});
    for (const auto& prior : next.outbox) if (prior.messageId == plain.messageId)
        return Result<WireEnvelope>::failure({ErrorCode::Conflict, "M4-01", "消息标识已使用"});
    if (!findTrusted(next, plain.to)) return Result<WireEnvelope>::failure({ErrorCode::IdentityChanged, "M4-01", "等待确认对端身份"});
    auto chosen = next.sessions.end();
    for (auto it = next.sessions.begin(); it != next.sessions.end(); ++it)
        if (it->peer == plain.to && (chosen == next.sessions.end() || it->sessionId < chosen->sessionId)) chosen = it;
    if (chosen == next.sessions.end()) return Result<WireEnvelope>::failure({ErrorCode::NotFound, "M4-01", "尚未建立会话"});
    try {
        const nlohmann::json inner = {{"version", 2}, {"message_id", plain.messageId}, {"logical_group_message_id", plain.logicalGroupMessageId},
            {"from", plain.from}, {"to", plain.to}, {"kind", plain.kind}, {"group_id", plain.groupId},
            {"timestamp", plain.timestamp}, {"body_base64", cc::base64Encode(plain.plaintext)}};
        auto ratchet = cc::ratchet::DoubleRatchet::fromJson(chosen->ratchetState);
        auto encrypted = ratchet.encrypt(inner.dump(), chosen->associatedData);
        if (chosen->pendingPrekey) {
            encrypted.header.ikPub = chosen->pendingPrekey->ikPub;
            encrypted.header.ekPub = chosen->pendingPrekey->ekPub;
            encrypted.header.opkId = chosen->pendingPrekey->opkId;
        }
        WireEnvelope result{plain.messageId, plain.from, plain.to, plain.kind, cc::base64Encode(encrypted.toJson().dump()), plain.groupId, plain.timestamp};
        chosen->ratchetState = ratchet.toJson();
        next.outbox.push_back(result);
        if (plain.kind == "chat") {
            auto prior = std::find_if(next.history.begin(), next.history.end(), [&](const auto& h) { return h.messageId == plain.messageId; });
            if (prior == next.history.end()) next.history.push_back({plain.messageId, plain.logicalGroupMessageId, plain.from, plain.to, plain.plaintext, plain.groupId, plain.timestamp, DeliveryState::Queued});
        }
        std::erase_if(next.pendingSends, [&](const auto& p) { return p.messageId == plain.messageId; });
        const auto expected = next.revision++;
        auto saved = store_.commit(expected, next); if (!saved) return Result<WireEnvelope>::failure(saved.error());
        return Result<WireEnvelope>::success(std::move(result));
    } catch (const std::exception&) { return Result<WireEnvelope>::failure({ErrorCode::CryptoFailure, "M4-01", "消息加密失败"}); }
}
Result<PlainEnvelope> SessionService::decryptAndCommit(const WireEnvelope& message) {
    auto error = [](ErrorCode code) { return Result<PlainEnvelope>::failure({code, "M4-01", "消息认证、路由绑定或本地状态不匹配"}); };
    auto loaded = store_.load(); if (!loaded) return Result<PlainEnvelope>::failure(loaded.error());
    auto next = loaded.value();
    if (!next.identity || message.to != next.account.username || message.from == message.to || message.from.empty() ||
        message.from.size() > 32 || message.messageId.empty() || message.messageId.size() > 128 || message.groupId < 0 ||
        !wire::kind(message.kind) || message.payloadBase64.size() > 7 * 1024 * 1024) return error(ErrorCode::ProtocolError);
    const auto digest = cc::base64Encode(cc::crypto::sha256(message.payloadBase64));
    for (const auto& prior : next.receivedMessages) if (prior.messageId == message.messageId) {
        if (prior.from == message.from && prior.to == message.to && prior.kind == message.kind &&
            prior.groupId == message.groupId && prior.ciphertextSha256 == digest) return error(ErrorCode::AlreadyProcessed);
        return error(ErrorCode::ProtocolError);
    }
    const auto* trusted = findTrusted(next, message.from);
    if (!trusted) return error(ErrorCode::IdentityChanged);
    try {
        cc::Bytes raw;
        wire::require(cc::base64Decode(message.payloadBase64, raw));
        const auto encoded = nlohmann::json::parse(raw);
        wire::require(wire::bytes(encoded, "dh", 32).size() == 32);
        wire::integer(encoded, "pn", UINT32_MAX); wire::integer(encoded, "n", UINT32_MAX);
        auto envelope = cc::ratchet::Envelope::fromJson(encoded); wire::require(envelope.has_value());
        wire::require(envelope->ciphertext.size() >= 16);
        std::string prekeyId;
        if (envelope->header.hasPrekey()) {
            wire::require(wire::bytes(encoded, "ik", 32).size() == 32 && wire::bytes(encoded, "ek", 32).size() == 32);
            wire::integer(encoded, "opk_id");
            if (envelope->header.ikPub != trusted->dhPublicKey) return error(ErrorCode::IdentityChanged);
            prekeyId = cc::base64Encode(cc::crypto::sha256(envelope->header.ikPub + envelope->header.ekPub + next.identity->identityDh.pub));
        }
        cc::Bytes plaintext;
        std::optional<SessionRecord> accepted;
        for (const auto& session : next.sessions) {
            if (session.peer != message.from || (!prekeyId.empty() && session.sessionId != prekeyId)) continue;
            auto ratchet = cc::ratchet::DoubleRatchet::fromJson(session.ratchetState);
            if (ratchet.decrypt(envelope->header, envelope->ciphertext, session.associatedData, plaintext) == cc::ratchet::DoubleRatchet::DecryptStatus::Ok) {
                accepted = session; accepted->ratchetState = ratchet.toJson(); accepted->pendingPrekey.reset(); break;
            }
        }
        bool consumedPrekey = false;
        if (!accepted && !prekeyId.empty()) {
            if (std::any_of(next.sessions.begin(), next.sessions.end(), [&](const auto& s) { return s.sessionId == prekeyId; })) return error(ErrorCode::AuthenticationFailed);
            cc::Bytes oneTime;
            if (envelope->header.opkId) {
                auto key = next.identity->oneTimePrivateKeys.find(envelope->header.opkId);
                if (key == next.identity->oneTimePrivateKeys.end()) return error(ErrorCode::AuthenticationFailed);
                oneTime = key->second;
            }
            auto root = cc::ratchet::x3dhRespond(next.identity->identityDh.priv, next.identity->signedPrekey.priv, oneTime,
                envelope->header.ikPub, envelope->header.ekPub);
            auto ratchet = cc::ratchet::DoubleRatchet::initAsResponder(root, next.identity->signedPrekey);
            auto aad = buildAd(next.identity->identityDh.pub, trusted->dhPublicKey);
            if (ratchet.decrypt(envelope->header, envelope->ciphertext, aad, plaintext) != cc::ratchet::DoubleRatchet::DecryptStatus::Ok)
                return error(ErrorCode::AuthenticationFailed);
            accepted = SessionRecord{message.from, prekeyId, aad, ratchet.toJson(), std::nullopt};
            consumedPrekey = true;
        }
        if (!accepted) return error(ErrorCode::AuthenticationFailed);
        const auto inner = nlohmann::json::parse(plaintext);
        wire::require(wire::integer(inner, "version", 2) == 2);
        PlainEnvelope result{wire::text(inner, "message_id", 128), wire::text(inner, "logical_group_message_id", 128, true),
            wire::text(inner, "from", 32), wire::text(inner, "to", 32), wire::text(inner, "kind", 32),
            wire::bytes(inner, "body_base64", 1024 * 1024, true), wire::integer(inner, "group_id"), wire::integer(inner, "timestamp")};
        if (result.messageId != message.messageId || result.from != message.from || result.to != message.to ||
            result.kind != message.kind || result.groupId != message.groupId) return error(ErrorCode::ProtocolError);
        if (result.kind == "chat") (void)nlohmann::json(result.plaintext).dump();
        auto existing = std::find_if(next.sessions.begin(), next.sessions.end(), [&](const auto& s) { return s.sessionId == accepted->sessionId; });
        if (existing == next.sessions.end()) {
            if (std::count_if(next.sessions.begin(), next.sessions.end(), [&](const auto& s) { return s.peer == message.from; }) >= 8)
                return error(ErrorCode::Forbidden);
            next.sessions.push_back(*accepted);
        } else *existing = *accepted;
        if (consumedPrekey && envelope->header.opkId) next.identity->oneTimePrivateKeys.erase(envelope->header.opkId);
        next.receivedMessages.push_back({result.messageId, result.from, result.to, result.kind, result.groupId, digest});
        if (next.receivedMessages.size() > 4096) next.receivedMessages.erase(next.receivedMessages.begin());
        if (result.kind == "chat") next.history.push_back({result.messageId, result.logicalGroupMessageId, result.from, result.to, result.plaintext, result.groupId, result.timestamp, DeliveryState::RecipientReceived});
        else next.pendingPayloads.push_back(result);
        const auto expected = next.revision++;
        auto saved = store_.commit(expected, next); if (!saved) return Result<PlainEnvelope>::failure(saved.error());
        return Result<PlainEnvelope>::success(std::move(result));
    } catch (const std::exception&) { return error(ErrorCode::ProtocolError); }
}
}  // namespace ccfw
