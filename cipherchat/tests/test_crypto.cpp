#include <doctest/doctest.h>

#include "common/crypto/crypto.hpp"

#include <string>
#include <vector>

namespace {

// 十六进制 <-> 字节串，便于写官方测试向量。
cc::Bytes fromHex(const std::string& hex) {
    REQUIRE(hex.size() % 2 == 0);
    cc::Bytes out(hex.size() / 2, '\0');
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
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

TEST_CASE("冻结向量：RFC 7748 §6.1 X25519 Diffie-Hellman") {
    const cc::Bytes alicePriv = fromHex(
        "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const cc::Bytes alicePub = fromHex(
        "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    const cc::Bytes bobPriv = fromHex(
        "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const cc::Bytes bobPub = fromHex(
        "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const cc::Bytes shared = fromHex(
        "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    // 公钥也从私钥导出核对
    CHECK(toHex(cc::crypto::x25519SharedSecret(alicePriv, bobPub)) ==
          toHex(shared));
    CHECK(toHex(cc::crypto::x25519SharedSecret(bobPriv, alicePub)) ==
          toHex(shared));
}

TEST_CASE("冻结向量：RFC 8032 §7.1 Ed25519 TEST 1（空消息）") {
    const cc::Bytes priv = fromHex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    const cc::Bytes pub = fromHex(
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const cc::Bytes sig = fromHex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    CHECK(cc::crypto::ed25519Verify(pub, "", sig));
    // 本实现对私钥重签应得到规范确定的同一签名
    CHECK(toHex(cc::crypto::ed25519Sign(priv, "")) == toHex(sig));
    // 单比特翻转消息 → 验签失败
    CHECK_FALSE(cc::crypto::ed25519Verify(pub, "x", sig));
}

TEST_CASE("冻结向量：GCM 规范（McGrew-Viega）Test Case 3/4（AES-256）") {
    // TC3：256 位全零密钥、96 位全零 IV、空明文、空 AAD
    const cc::Bytes key(32, '\0');
    const cc::Bytes iv(12, '\0');
    const cc::Bytes tc3Tag = fromHex("530f8afbc74536b9a963b4f1c4cb738b");
    const cc::Bytes sealed3 = cc::crypto::aesGcmEncrypt(key, iv, "", "");
    CHECK(toHex(sealed3) == toHex(tc3Tag));
    CHECK(cc::crypto::aesGcmDecrypt(key, iv, sealed3, "").has_value());

    // TC4：同密钥/IV，明文 16 个零字节
    const cc::Bytes plain(16, '\0');
    const cc::Bytes tc4Ct =
        fromHex("cea7403d4d606b6e074ec5d3baf39d18");
    const cc::Bytes tc4Tag =
        fromHex("d0d1c8a799996bf0265b98b5d48ab919");
    const cc::Bytes sealed4 = cc::crypto::aesGcmEncrypt(key, iv, plain, "");
    CHECK(toHex(sealed4) ==
          toHex(tc4Ct) + toHex(tc4Tag));  // 输出 = 密文||标签
    auto opened = cc::crypto::aesGcmDecrypt(key, iv, sealed4, "");
    REQUIRE(opened.has_value());
    CHECK(toHex(*opened) == toHex(plain));
}

TEST_CASE("冻结向量：RFC 7914 §11 PBKDF2-HMAC-SHA256") {
    const cc::Bytes salt = fromHex("73616c74");  // "salt"
    CHECK(toHex(cc::crypto::pbkdf2Sha256("password", salt, 1, 32)) ==
          "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(toHex(cc::crypto::pbkdf2Sha256("password", salt, 2, 32)) ==
          "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(toHex(cc::crypto::pbkdf2Sha256("password", salt, 4096, 32)) ==
          "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

TEST_CASE("HKDF-SHA256 满足 RFC 5869 测试向量 1") {
    const cc::Bytes ikm = fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    const cc::Bytes salt = fromHex("000102030405060708090a0b0c");
    const cc::Bytes info = fromHex("f0f1f2f3f4f5f6f7f8f9");
    const cc::Bytes okm =
        cc::crypto::hkdfSha256(ikm, salt, info, 42);
    CHECK(toHex(okm) ==
          "3cb25f25faacd57a90434f64d0362f2a"
          "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
          "34007208d5b887185865");
}

TEST_CASE("HKDF 空盐与显式零盐等价，info 影响输出") {
    const cc::Bytes ikm = cc::crypto::randomBytes(32);
    const cc::Bytes explicitZeroSalt(32, '\0');
    CHECK(cc::crypto::hkdfSha256(ikm, "", "a", 32) ==
          cc::crypto::hkdfSha256(ikm, explicitZeroSalt, "a", 32));
    CHECK(cc::crypto::hkdfSha256(ikm, "", "a", 32) !=
          cc::crypto::hkdfSha256(ikm, "", "b", 32));
}

TEST_CASE("Ed25519 签名验证：正常、篡改、错钥") {
    const auto kp = cc::crypto::generateEd25519();
    REQUIRE(kp.priv.size() == 32);
    REQUIRE(kp.pub.size() == 32);
    const std::string msg = " CipherChat 预共享密钥束 ";
    const cc::Bytes sig = cc::crypto::ed25519Sign(kp.priv, msg);
    REQUIRE(sig.size() == 64);
    CHECK(cc::crypto::ed25519Verify(kp.pub, msg, sig));

    cc::Bytes tampered = sig;
    tampered[10] = static_cast<char>(tampered[10] ^ 0x01);
    CHECK_FALSE(cc::crypto::ed25519Verify(kp.pub, msg, tampered));
    CHECK_FALSE(cc::crypto::ed25519Verify(kp.pub, msg + "x", sig));

    const auto other = cc::crypto::generateEd25519();
    CHECK_FALSE(cc::crypto::ed25519Verify(other.pub, msg, sig));
    CHECK_FALSE(cc::crypto::ed25519Verify("bad-pub", msg, sig));
}

TEST_CASE("X25519 双方协商出相同秘密且每次密钥不同") {
    const auto a = cc::crypto::generateX25519();
    const auto b = cc::crypto::generateX25519();
    const cc::Bytes s1 = cc::crypto::x25519SharedSecret(a.priv, b.pub);
    const cc::Bytes s2 = cc::crypto::x25519SharedSecret(b.priv, a.pub);
    REQUIRE(s1.size() == 32);
    CHECK(s1 == s2);

    const auto c = cc::crypto::generateX25519();
    CHECK(cc::crypto::x25519SharedSecret(a.priv, c.pub) != s1);

    // 低阶公钥点（全零）必须被拒绝
    const cc::Bytes zeroPub(32, '\0');
    CHECK_THROWS(cc::crypto::x25519SharedSecret(a.priv, zeroPub));
    CHECK_THROWS(cc::crypto::x25519SharedSecret(a.priv, "short"));
}

TEST_CASE("AES-256-GCM 往返、AAD 绑定与防篡改") {
    const cc::Bytes key = cc::crypto::randomBytes(32);
    const cc::Bytes nonce = cc::crypto::randomBytes(12);
    const std::string plain = "端到端加密消息体——hello 明文会被完整恢复";
    const std::string aad = "header:alice->bob";

    const cc::Bytes sealed = cc::crypto::aesGcmEncrypt(key, nonce, plain, aad);
    REQUIRE(sealed.size() == plain.size() + 16);
    auto opened = cc::crypto::aesGcmDecrypt(key, nonce, sealed, aad);
    REQUIRE(opened.has_value());
    CHECK(*opened == plain);

    // 换 AAD / 换 nonce / 换密钥 / 篡改密文，任何一处都解不开
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, nonce, sealed, "other").has_value());
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, nonce + "x", sealed, aad).has_value());
    CHECK_FALSE(
        cc::crypto::aesGcmDecrypt(cc::crypto::randomBytes(32), nonce, sealed, aad)
            .has_value());
    cc::Bytes bits = sealed;
    bits[0] = static_cast<char>(bits[0] ^ 0xFF);
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, nonce, bits, aad).has_value());
    bits = sealed;
    bits.back() = static_cast<char>(bits.back() ^ 0x01);  // 篡改标签
    CHECK_FALSE(cc::crypto::aesGcmDecrypt(key, nonce, bits, aad).has_value());

    // 空明文也应有标签输出且可往返
    const cc::Bytes emptySealed = cc::crypto::aesGcmEncrypt(key, nonce, "", "");
    CHECK(emptySealed.size() == 16);
    CHECK(cc::crypto::aesGcmDecrypt(key, nonce, emptySealed, "").has_value());
}

TEST_CASE("PBKDF2 确定性、迭代敏感与常数时间比较") {
    const cc::Bytes salt = cc::crypto::randomBytes(16);
    cc::Bytes h1 = cc::crypto::pbkdf2Sha256("口令pw123", salt, 1000, 32);
    const cc::Bytes h2 =
        cc::crypto::pbkdf2Sha256("口令pw123", salt, 1000, 32);
    CHECK(h1 == h2);
    CHECK(h1.size() == 32);
    // 迭代次数或口令或盐任一不同 → 哈希不同
    CHECK(cc::crypto::pbkdf2Sha256("口令pw123", salt, 1001, 32) != h1);
    CHECK(cc::crypto::pbkdf2Sha256("口令pw124", salt, 1000, 32) != h1);
    CHECK(cc::crypto::pbkdf2Sha256("口令pw123", cc::crypto::randomBytes(16),
                                   1000, 32) != h1);

    CHECK(cc::crypto::constantTimeEqual(h1, h2));
    (h1)[0] ^= 0x01;
    CHECK_FALSE(cc::crypto::constantTimeEqual(h1, h2));
    CHECK_FALSE(cc::crypto::constantTimeEqual("a", "ab"));
}

TEST_CASE("随机数不重复且长度正确") {
    const cc::Bytes a = cc::crypto::randomBytes(0);
    CHECK(a.empty());
    const cc::Bytes b = cc::crypto::randomBytes(32);
    const cc::Bytes c = cc::crypto::randomBytes(32);
    CHECK(b.size() == 32);
    CHECK(c.size() == 32);
    CHECK(b != c);  // 概率为 2^-256，可安全断言
}
