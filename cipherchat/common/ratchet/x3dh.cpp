#include "common/ratchet/x3dh.hpp"

#include <stdexcept>

namespace cc::ratchet {

namespace {

// 密钥派生固定 info，双端与版本强绑定。
constexpr BytesView kX3dhInfo = "CipherChat/X3DH/v1";

Bytes deriveRootKey(const Bytes& dh1, const Bytes& dh2, const Bytes& dh3,
                    const Bytes* dh4) {
    Bytes ikm;
    ikm.reserve(dh1.size() + dh2.size() + dh3.size() +
                (dh4 ? dh4->size() : 0));
    ikm.append(dh1).append(dh2).append(dh3);
    if (dh4) ikm.append(*dh4);
    return cc::crypto::hkdfSha256(ikm, "", kX3dhInfo, 32);
}

}  // namespace

X3dhInitiation x3dhInitiate(BytesView ikPriv, BytesView ikPub,
                            const X3dhPeerBundle& peer) {
    if (!cc::crypto::ed25519Verify(peer.signPub, peer.spkPub, peer.spkSig)) {
        throw std::runtime_error("x3dh: 预共享密钥束签名验证失败（公钥束可能被篡改）");
    }
    const bool useOpk = peer.opkId.has_value() && !peer.opkPub.empty();

    const auto ek = cc::crypto::generateX25519();
    const Bytes dh1 = cc::crypto::x25519SharedSecret(ikPriv, peer.spkPub);
    const Bytes dh2 = cc::crypto::x25519SharedSecret(ek.priv, peer.ikPub);
    const Bytes dh3 = cc::crypto::x25519SharedSecret(ek.priv, peer.spkPub);
    const Bytes dh4 = useOpk
                          ? cc::crypto::x25519SharedSecret(ek.priv, peer.opkPub)
                          : Bytes{};

    X3dhInitiation out;
    out.rootKey = deriveRootKey(dh1, dh2, dh3, useOpk ? &dh4 : nullptr);
    out.ikPub.assign(ikPub);
    out.ekPub = ek.pub;
    out.opkId = useOpk ? *peer.opkId : 0;
    return out;
}

cc::Bytes x3dhRespond(BytesView ikPriv, BytesView spkPriv, BytesView opkPriv,
                      BytesView initiatorIkPub, BytesView initiatorEkPub) {
    const Bytes dh1 = cc::crypto::x25519SharedSecret(spkPriv, initiatorIkPub);
    const Bytes dh2 = cc::crypto::x25519SharedSecret(ikPriv, initiatorEkPub);
    const Bytes dh3 = cc::crypto::x25519SharedSecret(spkPriv, initiatorEkPub);
    const bool useOpk = !opkPriv.empty();
    const Bytes dh4 =
        useOpk ? cc::crypto::x25519SharedSecret(opkPriv, initiatorEkPub)
               : Bytes{};
    return deriveRootKey(dh1, dh2, dh3, useOpk ? &dh4 : nullptr);
}

}  // namespace cc::ratchet
