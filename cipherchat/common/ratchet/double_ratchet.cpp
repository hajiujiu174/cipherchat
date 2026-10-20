#include "common/ratchet/double_ratchet.hpp"

#include <stdexcept>

namespace cc::ratchet {

namespace {

using cc::Bytes;
using cc::BytesView;
using cc::crypto::hkdfSha256;

constexpr BytesView kRatchetInfo = "CipherChat/Ratchet/v1";
constexpr BytesView kChainStepInfo = "CipherChat/ChainStep/v1";
constexpr BytesView kMessageKeyInfo = "CipherChat/MessageKey/v1";
constexpr BytesView kNonceInfo = "CipherChat/Nonce/v1";

// KDF_RK：DH 输出搅入根密钥，产出新根密钥 + 链密钥。
std::pair<Bytes, Bytes> kdfRk(BytesView rk, BytesView dhOut) {
    const Bytes out = hkdfSha256(dhOut, rk, kRatchetInfo, 64);
    return {Bytes(out.data(), out.data() + 32),
            Bytes(out.data() + 32, out.data() + 64)};
}

// KDF_CK：链密钥前进。返回 (新链密钥, 本条消息密钥)。
std::pair<Bytes, Bytes> kdfCk(BytesView ck) {
    Bytes nextCk = hkdfSha256(ck, "", kChainStepInfo, 32);
    Bytes messageKey = hkdfSha256(ck, "", kMessageKeyInfo, 32);
    return {std::move(nextCk), std::move(messageKey)};
}

// 消息密钥派生 AEAD nonce（消息密钥唯一 → nonce 唯一，永不重复）。
Bytes nonceFrom(BytesView messageKey) {
    return hkdfSha256(messageKey, "", kNonceInfo, crypto::kNonceLen);
}

std::string skipKey(BytesView remoteDhPub, std::uint32_t n) {
    return cc::base64Encode(remoteDhPub) + ":" + std::to_string(n);
}

// AEAD 附加数据 = 会话绑定 ad || 规范化头字节（防头部被篡改）。
Bytes buildAad(BytesView ad, const Header& h) {
    Bytes out(ad);
    out.append(h.dhPub);
    const unsigned char pn[4] = {static_cast<unsigned char>(h.pn >> 24),
                                 static_cast<unsigned char>(h.pn >> 16),
                                 static_cast<unsigned char>(h.pn >> 8),
                                 static_cast<unsigned char>(h.pn)};
    const unsigned char n[4] = {static_cast<unsigned char>(h.n >> 24),
                                static_cast<unsigned char>(h.n >> 16),
                                static_cast<unsigned char>(h.n >> 8),
                                static_cast<unsigned char>(h.n)};
    out.append(reinterpret_cast<const char*>(pn), 4);
    out.append(reinterpret_cast<const char*>(n), 4);
    return out;
}

Bytes b64DecodeOrEmpty(const nlohmann::json& j, const char* key) {
    Bytes out;
    const auto it = j.find(key);
    if (it != j.end() && it->is_string()) {
        cc::base64Decode(it->get<std::string>(), out);
    }
    return out;
}

std::uint32_t jsonU32(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_unsigned()
               ? static_cast<std::uint32_t>(it->get<std::uint64_t>())
               : 0;
}

}  // namespace

nlohmann::json Header::toJson() const {
    nlohmann::json j;
    j["dh"] = cc::base64Encode(dhPub);
    j["pn"] = pn;
    j["n"] = n;
    if (hasPrekey()) {
        j["ik"] = cc::base64Encode(ikPub);
        j["ek"] = cc::base64Encode(ekPub);
        j["opk_id"] = opkId;
    }
    return j;
}

Header Header::fromJson(const nlohmann::json& j) {
    Header h;
    h.dhPub = b64DecodeOrEmpty(j, "dh");
    h.pn = jsonU32(j, "pn");
    h.n = jsonU32(j, "n");
    h.ikPub = b64DecodeOrEmpty(j, "ik");
    h.ekPub = b64DecodeOrEmpty(j, "ek");
    const auto it = j.find("opk_id");
    h.opkId = it != j.end() && it->is_number_integer()
                  ? it->get<std::int64_t>()
                  : 0;
    return h;
}

nlohmann::json Envelope::toJson() const {
    nlohmann::json j = header.toJson();
    j["ct"] = cc::base64Encode(ciphertext);
    return j;
}

std::optional<Envelope> Envelope::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    Envelope e;
    e.header = Header::fromJson(j);
    if (e.header.dhPub.empty()) return std::nullopt;
    e.ciphertext = b64DecodeOrEmpty(j, "ct");
    return e;
}

DoubleRatchet DoubleRatchet::initAsInitiator(const Bytes& rootKey,
                                             BytesView peerSpkPub) {
    DoubleRatchet r;
    r.rk_ = rootKey;
    r.dhr_.assign(peerSpkPub);
    r.dhs_ = crypto::generateX25519();
    // 发起方立即完成一次 DH 棘轮，得到首条发送链
    std::tie(r.rk_, r.cks_) = kdfRk(r.rk_, crypto::x25519SharedSecret(
                                                r.dhs_.priv, r.dhr_));
    return r;
}

DoubleRatchet DoubleRatchet::initAsResponder(const Bytes& rootKey,
                                             const crypto::X25519KeyPair& spkPair) {
    DoubleRatchet r;
    r.rk_ = rootKey;
    r.dhs_ = {spkPair.priv, spkPair.pub};
    // dhr_/ckr_ 为空：收到首条消息的头部公钥后触发 DH 棘轮
    return r;
}

Envelope DoubleRatchet::encrypt(BytesView plaintext, BytesView ad) {
    if (cks_.empty()) {
        throw std::runtime_error(
            "DoubleRatchet: 发送链未建立（响应方尚未收到首条消息）");
    }
    Bytes messageKey;
    std::tie(cks_, messageKey) = kdfCk(cks_);

    Header h;
    h.dhPub = dhs_.pub;
    h.pn = pn_;
    h.n = ns_;
    ++ns_;

    Envelope e;
    e.header = std::move(h);
    e.ciphertext = crypto::aesGcmEncrypt(
        messageKey, nonceFrom(messageKey), plaintext, buildAad(ad, e.header));
    return e;
}

DoubleRatchet::DecryptStatus DoubleRatchet::decrypt(const Header& header,
                                                    BytesView ciphertext,
                                                    BytesView ad,
                                                    Bytes& plaintextOut) {
    // 认证原子性（contracts.md §5）：全程在候选副本上推进链/缓存，
    // 只有 AEAD 验证成功才提交。失败时本对象序列化后逐字节不变——
    // 篡改包之后，同序号的原包仍可正常解密。
    DoubleRatchet cand = *this;

    // 1) 乱序后到：查跳过密钥缓存（命中也不先删，验证成功才删）
    const std::string key = skipKey(header.dhPub, header.n);
    const auto cached = cand.skipped_.find(key);
    if (cached != cand.skipped_.end()) {
        const Bytes messageKey = cached->second;
        auto plain = crypto::aesGcmDecrypt(
            messageKey, nonceFrom(messageKey), ciphertext, buildAad(ad, header));
        if (!plain) return DecryptStatus::Failed;
        cand.skipped_.erase(cached);
        plaintextOut = std::move(*plain);
        *this = std::move(cand);
        return DecryptStatus::Ok;
    }

    // 2) 头部公钥变化 → 方向切换（补齐旧链跳过密钥后 DH 棘轮，均在候选上）
    if (header.dhPub != cand.dhr_) {
        if (!cand.ckr_.empty()) {
            if (const auto st = cand.skipMessageKeys(header.pn);
                st != DecryptStatus::Ok) {
                return st;
            }
        } else if (header.pn > 0) {
            return DecryptStatus::BadState;
        }
        cand.dhRatchet(header);
    }

    // 3) 补齐当前链中先于本条的消息密钥
    if (const auto st = cand.skipMessageKeys(header.n); st != DecryptStatus::Ok) {
        return st;
    }

    if (cand.ckr_.empty()) {
        return DecryptStatus::BadState;
    }
    Bytes messageKey;
    std::tie(cand.ckr_, messageKey) = kdfCk(cand.ckr_);
    ++cand.nr_;
    auto plain = crypto::aesGcmDecrypt(
        messageKey, nonceFrom(messageKey), ciphertext, buildAad(ad, header));
    if (!plain) return DecryptStatus::Failed;  // 候选被丢弃：状态零变化
    plaintextOut = std::move(*plain);
    *this = std::move(cand);
    return DecryptStatus::Ok;
}

void DoubleRatchet::dhRatchet(const Header& header) {
    pn_ = ns_;
    ns_ = 0;
    nr_ = 0;
    dhr_ = header.dhPub;
    // 先用旧密钥对与对端新公钥派生接收链……
    std::tie(rk_, ckr_) =
        kdfRk(rk_, crypto::x25519SharedSecret(dhs_.priv, dhr_));
    // ……再换新密钥对派生发送链
    dhs_ = crypto::generateX25519();
    std::tie(rk_, cks_) =
        kdfRk(rk_, crypto::x25519SharedSecret(dhs_.priv, dhr_));
}

DoubleRatchet::DecryptStatus DoubleRatchet::skipMessageKeys(
    std::uint32_t until) {
    if (ckr_.empty()) {
        return until == 0 ? DecryptStatus::Ok : DecryptStatus::BadState;
    }
    if (nr_ + kMaxSkip < until || until < nr_) {
        // 超出缓存上限或序号回绕，视为对端作恶/状态失步
        return DecryptStatus::TooManySkip;
    }
    while (nr_ < until) {
        Bytes messageKey;
        std::tie(ckr_, messageKey) = kdfCk(ckr_);
        skipped_[skipKey(dhr_, nr_)] = messageKey;
        ++nr_;
    }
    return DecryptStatus::Ok;
}

nlohmann::json DoubleRatchet::toJson() const {
    using cc::base64Encode;
    nlohmann::json j;
    j["dhs_priv"] = base64Encode(dhs_.priv);
    j["dhs_pub"] = base64Encode(dhs_.pub);
    j["dhr"] = base64Encode(dhr_);
    j["rk"] = base64Encode(rk_);
    j["cks"] = base64Encode(cks_);
    j["ckr"] = base64Encode(ckr_);
    j["ns"] = ns_;
    j["nr"] = nr_;
    j["pn"] = pn_;
    nlohmann::json sk = nlohmann::json::object();
    for (const auto& [k, v] : skipped_) sk[k] = base64Encode(v);
    j["skipped"] = std::move(sk);
    return j;
}

DoubleRatchet DoubleRatchet::fromJson(const nlohmann::json& j) {
    DoubleRatchet r;
    r.dhs_.priv = b64DecodeOrEmpty(j, "dhs_priv");
    r.dhs_.pub = b64DecodeOrEmpty(j, "dhs_pub");
    r.dhr_ = b64DecodeOrEmpty(j, "dhr");
    r.rk_ = b64DecodeOrEmpty(j, "rk");
    r.cks_ = b64DecodeOrEmpty(j, "cks");
    r.ckr_ = b64DecodeOrEmpty(j, "ckr");
    r.ns_ = jsonU32(j, "ns");
    r.nr_ = jsonU32(j, "nr");
    r.pn_ = jsonU32(j, "pn");
    const auto it = j.find("skipped");
    if (it != j.end() && it->is_object()) {
        for (auto elem = it->begin(); elem != it->end(); ++elem) {
            Bytes mk;
            if (elem.value().is_string() &&
                cc::base64Decode(elem.value().get<std::string>(), mk)) {
                r.skipped_[elem.key()] = std::move(mk);
            }
        }
    }
    return r;
}

}  // namespace cc::ratchet
