// M1-02 验收：棘轮解密认证原子性。
// 契约（docs/contracts.md §5）：decrypt 在候选副本上执行，失败恢复原状态
// ——包括 skipped keys 与一次性材料；篡改包之后同序号原包仍可解密；
// 失败前后序列化状态逐字节相同；64 条缓存边界与 65 条行为明确。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "common/crypto/crypto.hpp"
#include "common/ratchet/double_ratchet.hpp"
#include "common/ratchet/x3dh.hpp"

#include <string>
#include <vector>

namespace {

using namespace cc;
using ratchet::DoubleRatchet;
using ratchet::Envelope;
using ratchet::Header;

struct Peer {
    crypto::X25519KeyPair ik = crypto::generateX25519();
    crypto::Ed25519KeyPair sign = crypto::generateEd25519();
    crypto::X25519KeyPair spk = crypto::generateX25519();
    crypto::X25519KeyPair opk = crypto::generateX25519();
    Bytes spkSig = crypto::ed25519Sign(sign.priv, spk.pub);
};

Bytes adOf(const Peer& a, const Peer& b) {
    return a.ik.pub < b.ik.pub ? a.ik.pub + b.ik.pub : b.ik.pub + a.ik.pub;
}

struct Session {
    Peer alice, bob;
    Bytes ad = adOf(alice, bob);
    DoubleRatchet ar, br;

    Session() {
        ratchet::X3dhPeerBundle bundle;
        bundle.ikPub = bob.ik.pub;
        bundle.spkPub = bob.spk.pub;
        bundle.spkSig = bob.spkSig;
        bundle.signPub = bob.sign.pub;
        bundle.opkId = 7;
        bundle.opkPub = bob.opk.pub;
        const auto init = ratchet::x3dhInitiate(alice.ik.priv, alice.ik.pub,
                                                bundle);
        ar = DoubleRatchet::initAsInitiator(init.rootKey, bob.spk.pub);
        const Bytes bobRoot = ratchet::x3dhRespond(
            bob.ik.priv, bob.spk.priv, bob.opk.priv, init.ikPub, init.ekPub);
        REQUIRE(bobRoot == init.rootKey);
        br = DoubleRatchet::initAsResponder(bobRoot,
                                            {bob.spk.priv, bob.spk.pub});
        // 首条 prekey 消息建立 Bob 侧
        const Envelope first = ar.encrypt("first", ad);
        Bytes out;
        REQUIRE(br.decrypt(first.header, first.ciphertext, ad, out) ==
                DoubleRatchet::DecryptStatus::Ok);
    }

    Envelope aliceSays(const std::string& text) { return ar.encrypt(text, ad); }
    Envelope bobSays(const std::string& text) { return br.encrypt(text, ad); }
};

}  // namespace

TEST_CASE("M1-02 篡改包失败后原包仍可解密，失败前后状态逐字节相同") {
    Session s;
    auto env = s.aliceSays("机密内容");
    const auto before = s.br.toJson();
    Bytes out;

    cc::Bytes bits = env.ciphertext;
    bits[0] = static_cast<char>(bits[0] ^ 0x40);
    CHECK(s.br.decrypt(env.header, bits, s.ad, out) ==
          DoubleRatchet::DecryptStatus::Failed);
    CHECK(s.br.toJson() == before);

    // 原包立即可解（无需重建状态）
    CHECK(s.br.decrypt(env.header, env.ciphertext, s.ad, out) ==
          DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "机密内容");
    CHECK(s.br.toJson() != before);
}

TEST_CASE("M1-02 乱序缓存密钥不被失败的解密消费") {
    Session s;
    std::vector<Envelope> sent;
    for (int i = 0; i < 3; ++i) sent.push_back(s.aliceSays("o" + std::to_string(i)));
    Bytes out;
    REQUIRE(s.br.decrypt(sent[2].header, sent[2].ciphertext, s.ad, out) ==
            DoubleRatchet::DecryptStatus::Ok);
    REQUIRE(s.br.skippedKeys() == 2);
    const auto before = s.br.toJson();

    cc::Bytes bits = sent[0].ciphertext;
    bits[1] = static_cast<char>(bits[1] ^ 0x10);
    CHECK(s.br.decrypt(sent[0].header, bits, s.ad, out) ==
          DoubleRatchet::DecryptStatus::Failed);
    CHECK(s.br.skippedKeys() == 2);
    CHECK(s.br.toJson() == before);

    CHECK(s.br.decrypt(sent[0].header, sent[0].ciphertext, s.ad, out) ==
          DoubleRatchet::DecryptStatus::Ok);
    CHECK(s.br.skippedKeys() == 1);
}

TEST_CASE("M1-02 跨 DH 棘轮后认证失败同样零状态变化") {
    Session s;
    s.bobSays("方向切换");
    auto env = s.aliceSays("新链机密");
    Bytes out;
    REQUIRE(s.br.decrypt(env.header, env.ciphertext, s.ad, out) ==
            DoubleRatchet::DecryptStatus::Ok);
    // 方向再次切换后构造跨棘轮消息并篡改
    s.bobSays("再切一次");

    auto env3 = s.aliceSays("跨棘轮机密");
    const auto before = s.br.toJson();
    cc::Bytes bits = env3.ciphertext;
    bits.back() = static_cast<char>(bits.back() ^ 0x02);
    CHECK(s.br.decrypt(env3.header, bits, s.ad, out) ==
          DoubleRatchet::DecryptStatus::Failed);
    CHECK(s.br.toJson() == before);
    CHECK(s.br.decrypt(env3.header, env3.ciphertext, s.ad, out) ==
          DoubleRatchet::DecryptStatus::Ok);
}

TEST_CASE("M1-02 重放被拒且不破坏状态；64/65 缓存边界明确") {
    Session s;
    auto env = s.aliceSays("只此一条");
    Bytes out;
    REQUIRE(s.br.decrypt(env.header, env.ciphertext, s.ad, out) ==
            DoubleRatchet::DecryptStatus::Ok);
    const auto afterFirst = s.br.toJson();
    CHECK(s.br.decrypt(env.header, env.ciphertext, s.ad, out) !=
          DoubleRatchet::DecryptStatus::Ok);
    CHECK(s.br.toJson() == afterFirst);  // 重放失败同样零状态变化
    // 后续正常消息不受影响
    auto reply = s.bobSays("继续");
    Bytes replyOut;
    REQUIRE(s.ar.decrypt(reply.header, reply.ciphertext, s.ad, replyOut) ==
            DoubleRatchet::DecryptStatus::Ok);
    CHECK(replyOut == "继续");

    // 64 条跳过（kMaxSkip）允许
    Session t;
    std::vector<Envelope> sent;
    for (int i = 0; i < 65; ++i) sent.push_back(t.aliceSays("b" + std::to_string(i)));
    CHECK(t.br.decrypt(sent[64].header, sent[64].ciphertext, t.ad, out) ==
          DoubleRatchet::DecryptStatus::Ok);
    CHECK(t.br.skippedKeys() == 64);
    // 65 条 → 受控 TooManySkip，状态不变
    Session u;
    std::vector<Envelope> sent2;
    for (int i = 0; i < 66; ++i) sent2.push_back(u.aliceSays("c" + std::to_string(i)));
    CHECK(u.br.decrypt(sent2[65].header, sent2[65].ciphertext, u.ad, out) ==
          DoubleRatchet::DecryptStatus::TooManySkip);
    const auto beforeU = u.br.toJson();
    CHECK(u.br.decrypt(sent2[65].header, sent2[65].ciphertext, u.ad, out) ==
          DoubleRatchet::DecryptStatus::TooManySkip);
    CHECK(u.br.toJson() == beforeU);
}
