#pragma once
// 登录证明公式（双端共享，唯一来源）。
//
// 登录协议（TLS 之上再加一层口令证明，防重放）：
//   1) 客户端发 login_request{username}
//   2) 服务器发 login_challenge{nonce, salt, iterations}
//      nonce 为一次性随机数（16 字节），salt/iterations 为该账号注册时参数
//   3) 客户端计算
//        k     = PBKDF2-SHA256(password, salt, iterations, 32)
//        proof = HKDF(k, salt="CipherChat/Login/v1", info=nonce)
//      及 Ed25519 对 nonce 的签名（证明持有身份私钥）
//   4) 服务器重算 proof 并常数时间比较；nonce 用后即焚
//
// 服务器存储的 password_hash 即 k 本身（32 字节），无需重跑 PBKDF2：
// proof 只依赖 k 与 nonce，双方用同一公式派生后比较。
// 本模块属于双端公共层；客户端不得引用 server/ 下任何头文件。

#include "common/base64.hpp"
#include "common/crypto/crypto.hpp"

#include <cstddef>
#include <cstdint>

namespace cc::auth {

// PBKDF2 参数（课程设计取值，兼顾演示速度与基本强度）。
inline constexpr std::int64_t kPbkdf2Iterations = 200000;
inline constexpr std::size_t kPbkdf2SaltLen = 16;
inline constexpr std::size_t kPbkdf2KeyLen = 32;
inline constexpr std::size_t kLoginNonceLen = 16;

// 登录证明派生（客户端与服务器共用同一公式）。
// k 为 PBKDF2 输出（32 字节），nonce 为挑战随机数原始字节。
Bytes deriveLoginProof(const Bytes& k, const Bytes& nonce);

// 验证：由存档的 k 与 nonce 重算期望证明，与客户端提交值常数时间比较。
bool verifyLoginProof(const Bytes& storedK, const Bytes& nonce,
                      const Bytes& clientProof);

}  // namespace cc::auth
