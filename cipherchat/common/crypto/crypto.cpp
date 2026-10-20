#include "common/crypto/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace cc::crypto {

namespace {

// OpenSSL 调用失败：带上错误栈信息抛出。正常的密码学验证失败不走这里。
[[noreturn]] void throwOpenSsl(const char* what) {
    std::string msg = what;
    unsigned long err = ERR_peek_last_error();
    if (err != 0) {
        std::array<char, 256> buf{};
        ERR_error_string_n(err, buf.data(), buf.size());
        msg += ": ";
        msg += buf.data();
    }
    throw std::runtime_error(msg);
}

// EVP 上下文的 RAII 删除器集合。
struct EvpCtxDeleter {
    void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); }
    void operator()(EVP_PKEY_CTX* p) const { EVP_PKEY_CTX_free(p); }
    void operator()(EVP_CIPHER_CTX* p) const { EVP_CIPHER_CTX_free(p); }
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
};
using MdCtx = std::unique_ptr<EVP_MD_CTX, EvpCtxDeleter>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, EvpCtxDeleter>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, EvpCtxDeleter>;
using Pkey = std::unique_ptr<EVP_PKEY, EvpCtxDeleter>;

// 由原始私钥种子构造 EVP_PKEY。
Pkey loadRawPrivate(int typeId, BytesView priv, const char* what) {
    if (priv.size() != 32) {
        throw std::runtime_error(std::string(what) + ": 私钥长度非法");
    }
    Pkey key(EVP_PKEY_new_raw_private_key(typeId, nullptr,
                                          reinterpret_cast<const unsigned char*>(
                                              priv.data()),
                                          priv.size()));
    if (!key) throwOpenSsl("EVP_PKEY_new_raw_private_key");
    return key;
}

// 由原始公钥构造 EVP_PKEY（仅用于验签 / 对端公钥协商）。
Pkey loadRawPublic(int typeId, BytesView pub, const char* what) {
    if (pub.size() != 32) {
        throw std::runtime_error(std::string(what) + ": 公钥长度非法");
    }
    Pkey key(EVP_PKEY_new_raw_public_key(typeId, nullptr,
                                         reinterpret_cast<const unsigned char*>(
                                             pub.data()),
                                         pub.size()));
    if (!key) throwOpenSsl("EVP_PKEY_new_raw_public_key");
    return key;
}

Bytes rawPublicOf(const EVP_PKEY* key, std::size_t expectedLen,
                  const char* what) {
    Bytes pub(expectedLen, '\0');
    size_t len = pub.size();
    if (EVP_PKEY_get_raw_public_key(
            key, reinterpret_cast<unsigned char*>(pub.data()), &len) != 1 ||
        len != expectedLen) {
        throwOpenSsl(what);
    }
    return pub;
}

}  // namespace

Bytes randomBytes(std::size_t n) {
    Bytes out(n, '\0');
    if (n > 0 && RAND_bytes(reinterpret_cast<unsigned char*>(out.data()),
                            static_cast<int>(n)) != 1) {
        throwOpenSsl("RAND_bytes");
    }
    return out;
}

Ed25519KeyPair generateEd25519() {
    Pkey key;
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr));
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1) {
        throwOpenSsl("Ed25519 keygen init");
    }
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw) != 1 || raw == nullptr) {
        throwOpenSsl("Ed25519 keygen");
    }
    key.reset(raw);
    Ed25519KeyPair kp;
    kp.pub = rawPublicOf(key.get(), kEd25519PubLen, "Ed25519 pub");
    kp.priv.resize(kEd25519PrivLen);
    size_t len = kp.priv.size();
    if (EVP_PKEY_get_raw_private_key(
            key.get(), reinterpret_cast<unsigned char*>(kp.priv.data()),
            &len) != 1 ||
        len != kEd25519PrivLen) {
        throwOpenSsl("Ed25519 priv");
    }
    return kp;
}

Bytes ed25519Sign(BytesView priv, BytesView msg) {
    Pkey key = loadRawPrivate(EVP_PKEY_ED25519, priv, "ed25519Sign");
    MdCtx ctx(EVP_MD_CTX_new());
    if (!ctx) throwOpenSsl("EVP_MD_CTX_new");
    // Ed25519 内置哈希，初始化时摘要算法必须为 NULL。
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr,
                           key.get()) != 1) {
        throwOpenSsl("EVP_DigestSignInit");
    }
    Bytes sig(kEd25519SigLen, '\0');
    size_t sigLen = sig.size();
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(sig.data()),
                       &sigLen,
                       reinterpret_cast<const unsigned char*>(msg.data()),
                       msg.size()) != 1 ||
        sigLen != kEd25519SigLen) {
        throwOpenSsl("EVP_DigestSign");
    }
    return sig;
}

bool ed25519Verify(BytesView pub, BytesView msg, BytesView sig) {
    if (pub.size() != kEd25519PubLen || sig.size() != kEd25519SigLen) {
        return false;
    }
    Pkey key;
    try {
        key = loadRawPublic(EVP_PKEY_ED25519, pub, "ed25519Verify");
    } catch (const std::runtime_error&) {
        return false;  // 公钥点非法，视为验签失败
    }
    MdCtx ctx(EVP_MD_CTX_new());
    if (!ctx) throwOpenSsl("EVP_MD_CTX_new");
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr,
                             key.get()) != 1) {
        throwOpenSsl("EVP_DigestVerifyInit");
    }
    return EVP_DigestVerify(ctx.get(),
                            reinterpret_cast<const unsigned char*>(sig.data()),
                            sig.size(),
                            reinterpret_cast<const unsigned char*>(msg.data()),
                            msg.size()) == 1;
}

X25519KeyPair generateX25519() {
    Pkey key;
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr));
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1) {
        throwOpenSsl("X25519 keygen init");
    }
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw) != 1 || raw == nullptr) {
        throwOpenSsl("X25519 keygen");
    }
    key.reset(raw);
    X25519KeyPair kp;
    kp.pub = rawPublicOf(key.get(), kX25519PubLen, "X25519 pub");
    kp.priv.resize(kX25519PrivLen);
    size_t len = kp.priv.size();
    if (EVP_PKEY_get_raw_private_key(
            key.get(), reinterpret_cast<unsigned char*>(kp.priv.data()),
            &len) != 1 ||
        len != kX25519PrivLen) {
        throwOpenSsl("X25519 priv");
    }
    return kp;
}

Bytes x25519SharedSecret(BytesView priv, BytesView peerPub) {
    Pkey own = loadRawPrivate(EVP_PKEY_X25519, priv, "x25519");
    Pkey peer = loadRawPublic(EVP_PKEY_X25519, peerPub, "x25519");
    PkeyCtx ctx(EVP_PKEY_CTX_new(own.get(), nullptr));
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1) {
        throwOpenSsl("EVP_PKEY_derive_init");
    }
    if (EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) != 1) {
        throwOpenSsl("EVP_PKEY_derive_set_peer");
    }
    Bytes secret(kSharedSecretLen, '\0');
    size_t len = secret.size();
    if (EVP_PKEY_derive(ctx.get(),
                        reinterpret_cast<unsigned char*>(secret.data()),
                        &len) != 1 ||
        len != kSharedSecretLen) {
        throwOpenSsl("EVP_PKEY_derive");
    }
    // RFC 7748：全零输出说明对端发送了低阶公钥点，必须拒绝。
    bool allZero = true;
    for (char c : secret) {
        if (c != '\0') {
            allZero = false;
            break;
        }
    }
    if (allZero) throw std::runtime_error("x25519: 共享秘密为全零（非法对端公钥）");
    return secret;
}

Bytes hkdfSha256(BytesView ikm, BytesView salt, BytesView info,
                 std::size_t outLen) {
    // 空盐按 RFC 5869 折叠为 HashLen 个零字节。
    static const Bytes kZeroSalt(32, '\0');
    if (salt.empty()) salt = kZeroSalt;
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1) {
        throwOpenSsl("HKDF init");
    }
    if (EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) != 1 ||
        EVP_PKEY_CTX_set1_hkdf_salt(
            ctx.get(), reinterpret_cast<const unsigned char*>(salt.data()),
            static_cast<int>(salt.size())) != 1 ||
        EVP_PKEY_CTX_set1_hkdf_key(
            ctx.get(), reinterpret_cast<const unsigned char*>(ikm.data()),
            static_cast<int>(ikm.size())) != 1 ||
        EVP_PKEY_CTX_add1_hkdf_info(
            ctx.get(), reinterpret_cast<const unsigned char*>(info.data()),
            static_cast<int>(info.size())) != 1) {
        throwOpenSsl("HKDF 参数设置");
    }
    Bytes out(outLen, '\0');
    size_t len = out.size();
    if (EVP_PKEY_derive(ctx.get(), reinterpret_cast<unsigned char*>(out.data()),
                        &len) != 1 ||
        len != out.size()) {
        throwOpenSsl("HKDF derive");
    }
    return out;
}

Bytes pbkdf2Sha256(BytesView password, BytesView salt,
                   std::uint64_t iterations, std::size_t outLen) {
    if (iterations == 0 || outLen == 0) {
        throw std::runtime_error("pbkdf2: 参数非法");
    }
    Bytes out(outLen, '\0');
    const int ok = PKCS5_PBKDF2_HMAC(
        password.data(), static_cast<int>(password.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()), static_cast<long>(iterations),
        EVP_sha256(), static_cast<int>(outLen),
        reinterpret_cast<unsigned char*>(out.data()));
    if (ok != 1) throwOpenSsl("PKCS5_PBKDF2_HMAC");
    return out;
}

Bytes sha256(BytesView data) {
    Bytes output(32, '\0');
    unsigned int length = 0;
    if (EVP_Digest(data.data(), data.size(), reinterpret_cast<unsigned char*>(output.data()),
                   &length, EVP_sha256(), nullptr) != 1 || length != 32)
        throw std::runtime_error("SHA-256 failed");
    return output;
}

bool constantTimeEqual(BytesView a, BytesView b) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

Bytes aesGcmEncrypt(BytesView key, BytesView nonce, BytesView plain,
                    BytesView aad) {
    if (key.size() != kAesKeyLen) {
        throw std::runtime_error("aesGcmEncrypt: 密钥须为 32 字节");
    }
    if (nonce.size() != kNonceLen) {
        throw std::runtime_error("aesGcmEncrypt: nonce 须为 12 字节");
    }
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throwOpenSsl("EVP_CIPHER_CTX_new");
    int outLen = 0;
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr,
                           nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(kNonceLen), nullptr) != 1 ||
        EVP_EncryptInit_ex(
            ctx.get(), nullptr, nullptr,
            reinterpret_cast<const unsigned char*>(key.data()),
            reinterpret_cast<const unsigned char*>(nonce.data())) != 1) {
        throwOpenSsl("AES-GCM init");
    }
    if (!aad.empty() &&
        EVP_EncryptUpdate(ctx.get(), nullptr, &outLen,
                          reinterpret_cast<const unsigned char*>(aad.data()),
                          static_cast<int>(aad.size())) != 1) {
        throwOpenSsl("AES-GCM aad");
    }
    Bytes out(plain.size() + kTagLen, '\0');
    if (!plain.empty() &&
        EVP_EncryptUpdate(
            ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &outLen,
            reinterpret_cast<const unsigned char*>(plain.data()),
            static_cast<int>(plain.size())) != 1) {
        throwOpenSsl("AES-GCM update");
    }
    // GCM 的 final 不产生输出，但会计算标签。
    unsigned char tail[1];
    int tailLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), tail, &tailLen) != 1) {
        throwOpenSsl("AES-GCM final");
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(kTagLen),
                            out.data() + plain.size()) != 1) {
        throwOpenSsl("AES-GCM get tag");
    }
    return out;
}

std::optional<Bytes> aesGcmDecrypt(BytesView key, BytesView nonce,
                                   BytesView cipherAndTag, BytesView aad) {
    if (key.size() != kAesKeyLen || nonce.size() != kNonceLen ||
        cipherAndTag.size() < kTagLen) {
        return std::nullopt;
    }
    const std::size_t cipherLen = cipherAndTag.size() - kTagLen;
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throwOpenSsl("EVP_CIPHER_CTX_new");
    int outLen = 0;
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr,
                           nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(kNonceLen), nullptr) != 1 ||
        EVP_DecryptInit_ex(
            ctx.get(), nullptr, nullptr,
            reinterpret_cast<const unsigned char*>(key.data()),
            reinterpret_cast<const unsigned char*>(nonce.data())) != 1) {
        throwOpenSsl("AES-GCM init");
    }
    if (!aad.empty() &&
        EVP_DecryptUpdate(ctx.get(), nullptr, &outLen,
                          reinterpret_cast<const unsigned char*>(aad.data()),
                          static_cast<int>(aad.size())) != 1) {
        throwOpenSsl("AES-GCM aad");
    }
    Bytes out(cipherLen, '\0');
    if (cipherLen > 0 &&
        EVP_DecryptUpdate(
            ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &outLen,
            reinterpret_cast<const unsigned char*>(cipherAndTag.data()),
            static_cast<int>(cipherLen)) != 1) {
        throwOpenSsl("AES-GCM update");
    }
    // 先填入期望标签；final 返回 0 即认证失败。
    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagLen),
            const_cast<void*>(static_cast<const void*>(
                cipherAndTag.data() + cipherLen))) != 1) {
        throwOpenSsl("AES-GCM set tag");
    }
    unsigned char tail[1];
    int tailLen = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), tail, &tailLen) != 1) {
        return std::nullopt;  // 标签不匹配：密文或 AAD 被篡改
    }
    return out;
}

}  // namespace cc::crypto
