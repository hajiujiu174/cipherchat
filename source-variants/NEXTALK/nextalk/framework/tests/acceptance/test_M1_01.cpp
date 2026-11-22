// M1-01 验收：加密原语冻结向量（来源均为公开标准，随机往返不可替代）。
// 链接 ccfw_existing（原 common/crypto 单一来源），不复制算法。
// 向量清单：
//   HKDF-SHA256    RFC 5869 §A.1 Test Case 1
//   X25519         RFC 7748 §6.1 Diffie-Hellman
//   Ed25519        RFC 8032 §7.1 TEST 1（空消息）
//   AES-256-GCM    GCM 规范（McGrew-Viega）Test Case 3/4
//   PBKDF2-SHA256  RFC 7914 §11（c=1/2/4096）
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "common/crypto/crypto.hpp"

namespace {

cc::Bytes fromHex(const std::string& hex) {
    REQUIRE(hex.size() % 2 == 0);
    cc::Bytes out(hex.size() / 2, '\0');
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(
            std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }
    return out;
}

std::string toHex(cc::BytesView bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(bytes[i]);
        out[i * 2] = kDigits[b >> 4];
        out[i * 2 + 1] = kDigits[b & 0xF];
    }
    return out;
}

}  // namespace

TEST_CASE("M1-01 HKDF-SHA256 RFC 5869 §A.1 向量 1") {
    const cc::Bytes ikm = fromHex(
        "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    const cc::Bytes salt = fromHex("000102030405060708090a0b0c");
    const cc::Bytes info = fromHex("f0f1f2f3f4f5f6f7f8f9");
    CHECK(toHex(cc::crypto::hkdfSha256(ikm, salt, info, 42)) ==
          "3cb25f25faacd57a90434f64d0362f2a"
          "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
          "34007208d5b887185865");
}

TEST_CASE("M1-01 X25519 RFC 7748 §6.1 向量") {
    const cc::Bytes alicePriv = fromHex(
        "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const cc::Bytes bobPub = fromHex(
        "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const cc::Bytes bobPriv = fromHex(
        "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const cc::Bytes alicePub = fromHex(
        "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    const cc::Bytes shared = fromHex(
        "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    CHECK(toHex(cc::crypto::x25519SharedSecret(alicePriv, bobPub)) ==
          toHex(shared));
    CHECK(toHex(cc::crypto::x25519SharedSecret(bobPriv, alicePub)) ==
          toHex(shared));
}

TEST_CASE("M1-01 Ed25519 RFC 8032 §7.1 TEST 1") {
    const cc::Bytes priv = fromHex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    const cc::Bytes pub = fromHex(
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const cc::Bytes sig = fromHex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    CHECK(cc::crypto::ed25519Verify(pub, "", sig));
    CHECK(toHex(cc::crypto::ed25519Sign(priv, "")) == toHex(sig));
}

TEST_CASE("M1-01 AES-256-GCM GCM 规范 TC3/TC4") {
    const cc::Bytes key(32, '\0');
    const cc::Bytes iv(12, '\0');
    CHECK(toHex(cc::crypto::aesGcmEncrypt(key, iv, "", "")) ==
          "530f8afbc74536b9a963b4f1c4cb738b");
    const cc::Bytes plain(16, '\0');
    CHECK(toHex(cc::crypto::aesGcmEncrypt(key, iv, plain, "")) ==
          "cea7403d4d606b6e074ec5d3baf39d18"
          "d0d1c8a799996bf0265b98b5d48ab919");
}

TEST_CASE("M1-01 PBKDF2-SHA256 RFC 7914 §11 向量") {
    const cc::Bytes salt = fromHex("73616c74");
    CHECK(toHex(cc::crypto::pbkdf2Sha256("password", salt, 1, 32)) ==
          "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(toHex(cc::crypto::pbkdf2Sha256("password", salt, 2, 32)) ==
          "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(toHex(cc::crypto::pbkdf2Sha256("password", salt, 4096, 32)) ==
          "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

TEST_CASE("M1-01 认证失败不输出明文；尺寸错误受控拒绝") {
    const cc::Bytes key = cc::crypto::randomBytes(32);
    const cc::Bytes nonce = cc::crypto::randomBytes(12);
    const cc::Bytes sealed = cc::crypto::aesGcmEncrypt(key, nonce, "机密", "aad");
    // 篡改 → nullopt，绝不输出部分明文
    cc::Bytes bits = sealed;
    bits[0] = static_cast<char>(bits[0] ^ 0x01);
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, nonce, bits, "aad").has_value());
    // 尺寸错误 → 受控 nullopt（不抛异常）
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, nonce, "short", "aad")
                    .has_value());
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, "badnonce", sealed, "aad")
                    .has_value());
    // 非法公钥输入 → 受控失败
    CHECK_FALSE(cc::crypto::ed25519Verify("bad", "", cc::Bytes(64, '\0')));
    const cc::Bytes zeroPub(32, '\0');
    CHECK_THROWS(cc::crypto::x25519SharedSecret(
        cc::crypto::randomBytes(32), zeroPub));
}
