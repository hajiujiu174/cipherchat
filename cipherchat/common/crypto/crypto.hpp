#pragma once
// 密码学原语封装（全部走 OpenSSL 3 EVP 接口，RAII 管理上下文）。
// 上层（X3DH / Double Ratchet / TLS 之外的载荷加密）只依赖本头文件。
//
// 约定：
//   - 二进制材料统一用 cc::Bytes（std::string 别名）；
//   - 失败视为程序错误（参数 bug / OpenSSL 故障），抛 std::runtime_error，
//     唯一例外是"验签/解密不通过"这类正常业务失败，返回 bool / optional。

#include "common/base64.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace cc::crypto {

// ---- 尺寸常量（字节）----
inline constexpr std::size_t kEd25519PrivLen = 32;  // 种子
inline constexpr std::size_t kEd25519PubLen = 32;
inline constexpr std::size_t kEd25519SigLen = 64;
inline constexpr std::size_t kX25519PrivLen = 32;
inline constexpr std::size_t kX25519PubLen = 32;
inline constexpr std::size_t kSharedSecretLen = 32;
inline constexpr std::size_t kAesKeyLen = 32;  // AES-256
inline constexpr std::size_t kNonceLen = 12;   // GCM 推荐 96 bit
inline constexpr std::size_t kTagLen = 16;     // GCM 认证标签

// CSPRNG 随机字节。
Bytes randomBytes(std::size_t n);
// SHA-256 原始 32 字节摘要，用于密文去重和文件完整性。
Bytes sha256(BytesView data);

// ---- Ed25519（身份签名）----

struct Ed25519KeyPair {
    Bytes priv;  // 32 字节种子
    Bytes pub;   // 32 字节公钥
};
Ed25519KeyPair generateEd25519();

// 用私钥种子对消息签名，输出 64 字节签名。
Bytes ed25519Sign(BytesView priv, BytesView msg);

// 验签。公钥长度非法或签名不通过均返回 false，不抛异常。
bool ed25519Verify(BytesView pub, BytesView msg, BytesView sig);

// ---- X25519（临时密钥协商）----

struct X25519KeyPair {
    Bytes priv;
    Bytes pub;
};
X25519KeyPair generateX25519();

// ECDH 共享秘密。对端公钥非法、协商结果为全零（对端小动作）时抛异常。
Bytes x25519SharedSecret(BytesView priv, BytesView peerPub);

// ---- HKDF-SHA256（密钥派生）----
// salt 为空时按 RFC 5869 以 32 字节全零盐处理。
Bytes hkdfSha256(BytesView ikm, BytesView salt, BytesView info,
                 std::size_t outLen);

// ---- PBKDF2-SHA256（口令拉伸，登录用）----
// iterations 建议 >= 200000。返回值长度 outLen。
Bytes pbkdf2Sha256(BytesView password, BytesView salt,
                   std::uint64_t iterations, std::size_t outLen);

// 常数时间比较（比较口令哈希/证明，防时序侧信道）。
bool constantTimeEqual(BytesView a, BytesView b);

// ---- AES-256-GCM（认证加密）----
// 加密输出 = 密文 || 16 字节标签；nonce 须 12 字节且永不重复使用。
// aad 可为空：仅参与认证不加密。
Bytes aesGcmEncrypt(BytesView key, BytesView nonce, BytesView plain,
                    BytesView aad);

// 解密并验签。密文/标签/AAD 被篡改或密钥不符时返回 nullopt。
std::optional<Bytes> aesGcmDecrypt(BytesView key, BytesView nonce,
                                   BytesView cipherAndTag, BytesView aad);

}  // namespace cc::crypto
