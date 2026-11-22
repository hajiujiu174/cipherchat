#include "common/auth/login_proof.hpp"

#include "common/crypto/crypto.hpp"

namespace cc::auth {

namespace {
constexpr cc::BytesView kLoginSalt = "Cipher" "Chat/Login/v1";
}  // namespace

// 从密码派生结果和服务端挑战计算本次登录的证明。
Bytes deriveLoginProof(const Bytes& k, const Bytes& nonce) {
    return cc::crypto::hkdfSha256(k, kLoginSalt, nonce, 32);
}

// 核对登录证明长度，并以恒定时间比较服务端计算值与客户端提交值。
bool verifyLoginProof(const Bytes& storedK, const Bytes& nonce,
                      const Bytes& clientProof) {
    if (storedK.size() != cc::crypto::kAesKeyLen || nonce.empty() ||
        clientProof.size() != 32) {
        return false;
    }
    return cc::crypto::constantTimeEqual(deriveLoginProof(storedK, nonce),
                                         clientProof);
}

}  // namespace cc::auth
