#pragma once
// 历史客户端的挑战应答公式，仅供兼容性回归使用。

#include "common/auth/password.hpp"
#include "common/base64.hpp"
#include "common/crypto/crypto.hpp"

#include <cstddef>
#include <cstdint>

namespace cc::auth {

inline constexpr std::size_t kLoginNonceLen = 16;

// 登录证明派生（客户端与服务器共用同一公式）。
// k 为 PBKDF2 输出（32 字节），nonce 为挑战随机数原始字节。
Bytes deriveLoginProof(const Bytes& k, const Bytes& nonce);

// 验证：由存档的 k 与 nonce 重算期望证明，与客户端提交值常数时间比较。
bool verifyLoginProof(const Bytes& storedK, const Bytes& nonce,
                      const Bytes& clientProof);

}  // namespace cc::auth
