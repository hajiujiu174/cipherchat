#include "common/auth/login_proof.hpp"

#include "common/crypto/crypto.hpp"

namespace cc::auth {

namespace {
constexpr cc::BytesView kLoginSalt = "CipherChat/Login/v1";
}  // namespace

Bytes deriveLoginProof(const Bytes& k, const Bytes& nonce) {
    return cc::crypto::hkdfSha256(k, kLoginSalt, nonce, 32);
}

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
