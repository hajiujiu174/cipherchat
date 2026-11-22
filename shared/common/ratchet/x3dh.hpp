#pragma once
// X3DH 密钥建立（Signal 规范的工程简化版）。
//
// 每用户长期密钥：
//   IK  = X25519 身份密钥对（参与 DH）
//   SignK = Ed25519 签名密钥对（给 SPK 签名，防服务器替换公钥束）
//   SPK = X25519 签名预共享密钥（可轮换，初始远端棘轮公钥）
//   OPK = 一批 X25519 一次性预共享密钥（上传服务器，用一次即删）
//
// 发起方计算（对端为 Bob）：
//   DH1 = X25519(IK_A_priv, SPK_B)
//   DH2 = X25519(EK_A_priv, IK_B)          EK_A 为本会话临时密钥
//   DH3 = X25519(EK_A_priv, SPK_B)
//   DH4 = X25519(EK_A_priv, OPK_B)         若 OPK 已耗尽则省略（弱化保护）
//   rootKey = HKDF-SHA256(DH1||DH2||DH3[||DH4], salt=空, info=固定串)
// 响应方用自己的私钥与发起方随首条消息带来的公钥按相同顺序重算。
//
// 本实现不加密首条"预密钥消息"的应用内容绑定（Signal 用 AD 绑定双方身份公钥，
// 由 Double Ratchet 的 ad 参数完成），这里只负责派生 rootKey。

#include "common/base64.hpp"
#include "common/crypto/crypto.hpp"

#include <cstdint>
#include <optional>

namespace cc::ratchet {

// 对端公钥束（自服务器 fetch_bundle 而来，字段为原始字节）。
struct X3dhPeerBundle {
    cc::Bytes ikPub;    // 对端 X25519 身份公钥
    cc::Bytes spkPub;   // 对端签名预共享密钥 X25519 公钥
    cc::Bytes spkSig;   // 对端 Ed25519 对 spkPub 的签名
    cc::Bytes signPub;  // 对端 Ed25519 验签公钥
    std::optional<std::int64_t> opkId;
    cc::Bytes opkPub;  // 一次性预共享公钥；opkId 无值时为空
};

// 发起方 X3DH 输出：rootKey 交给 Double Ratchet，其余随首条消息明文带给对端。
struct X3dhInitiation {
    cc::Bytes rootKey;
    cc::Bytes ikPub;  // 发起方 X25519 身份公钥
    cc::Bytes ekPub;  // 发起方临时公钥
    std::int64_t opkId = 0;  // 消耗的一次性密钥 id；0 = 未使用
};

// 发起方入口。先验 SPK 签名（失败抛异常），再生成临时密钥并派生 rootKey。
X3dhInitiation x3dhInitiate(BytesView ikPriv, BytesView ikPub,
                            const X3dhPeerBundle& peer);

// 响应方入口：以自己的私钥与首条消息带来的发起方公钥重算 rootKey。
// opkPriv 可为空（对应发起方未用 OPK 的弱化情形）。
cc::Bytes x3dhRespond(BytesView ikPriv, BytesView spkPriv, BytesView opkPriv,
                      BytesView initiatorIkPub, BytesView initiatorEkPub);

}  // namespace cc::ratchet
