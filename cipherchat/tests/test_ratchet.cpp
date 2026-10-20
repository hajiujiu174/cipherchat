#include <doctest/doctest.h>

#include "common/ratchet/double_ratchet.hpp"
#include "common/ratchet/x3dh.hpp"

#include <string>
#include <vector>

namespace {

// 测试用双人身份材料
struct Identity {
    cc::crypto::X25519KeyPair ik;      // 身份 DH 密钥
    cc::crypto::Ed25519KeyPair sign;   // 签名密钥
    cc::crypto::X25519KeyPair spk;     // 签名预共享密钥
    cc::crypto::X25519KeyPair opk;     // 一次性预共享密钥
    cc::Bytes spkSig;

    Identity()
        : ik(cc::crypto::generateX25519()),
          sign(cc::crypto::generateEd25519()),
          spk(cc::crypto::generateX25519()),
          opk(cc::crypto::generateX25519()) {
        spkSig = cc::crypto::ed25519Sign(sign.priv, spk.pub);
    }

    cc::ratchet::X3dhPeerBundle bundle(std::int64_t opkId) const {
        cc::ratchet::X3dhPeerBundle b;
        b.ikPub = ik.pub;
        b.spkPub = spk.pub;
        b.spkSig = spkSig;
        b.signPub = sign.pub;
        if (opkId > 0) {
            b.opkId = opkId;
            b.opkPub = opk.pub;
        }
        return b;
    }
};

// 会话级 AAD：双端一致即可（真实客户端用双方身份公钥拼接）
cc::Bytes sessionAd(const Identity& a, const Identity& b) {
    return a.ik.pub + b.ik.pub;
}

// 走完整 X3DH + 首条 prekey 消息，把双端棘轮都立起来
struct Conversation {
    Identity alice;
    Identity bob;
    cc::ratchet::DoubleRatchet ar;
    cc::ratchet::DoubleRatchet br;
    cc::Bytes ad;

    Conversation() {
        using namespace cc::ratchet;
        const auto init = x3dhInitiate(alice.ik.priv, alice.ik.pub,
                                       bob.bundle(7));
        ar = DoubleRatchet::initAsInitiator(init.rootKey, bob.spk.pub);

        const cc::Bytes bobRoot =
            x3dhRespond(bob.ik.priv, bob.spk.priv, bob.opk.priv, init.ikPub,
                        init.ekPub);
        REQUIRE(bobRoot == init.rootKey);
        br = DoubleRatchet::initAsResponder(bobRoot, bob.spk);

        ad = sessionAd(alice, bob);
        // 首条消息同时完成 Bob 侧会话建立
        const Envelope first = ar.encrypt("你好，Bob", ad);
        firstEnvelope = first;
        REQUIRE(br.decrypt(first.header, first.ciphertext, ad, plain) ==
                DoubleRatchet::DecryptStatus::Ok);
        REQUIRE(plain == "你好，Bob");
    }

    cc::ratchet::Envelope firstEnvelope;
    cc::Bytes plain;

    std::string aliceSays(const std::string& text) {
        auto env = ar.encrypt(text, ad);
        cc::Bytes out;
        REQUIRE(br.decrypt(env.header, env.ciphertext, ad, out) ==
                cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
        return out;
    }

    std::string bobSays(const std::string& text) {
        auto env = br.encrypt(text, ad);
        cc::Bytes out;
        REQUIRE(ar.decrypt(env.header, env.ciphertext, ad, out) ==
                cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
        return out;
    }
};

}  // namespace

TEST_CASE("X3DH：双端派生相同根密钥；篡改签名束被拒绝") {
    Identity alice, bob;
    const auto init = cc::ratchet::x3dhInitiate(alice.ik.priv, alice.ik.pub,
                                                bob.bundle(1));
    REQUIRE(init.rootKey.size() == 32);
    REQUIRE(init.opkId == 1);
    const cc::Bytes bobRoot = cc::ratchet::x3dhRespond(
        bob.ik.priv, bob.spk.priv, bob.opk.priv, init.ikPub, init.ekPub);
    CHECK(bobRoot == init.rootKey);

    // 无一次性密钥（服务器 OPK 耗尽）时仍可建立，只是弱化
    const auto init2 = cc::ratchet::x3dhInitiate(alice.ik.priv, alice.ik.pub,
                                                 bob.bundle(0));
    CHECK(init2.opkId == 0);
    const cc::Bytes bobRoot2 = cc::ratchet::x3dhRespond(
        bob.ik.priv, bob.spk.priv, "", init2.ikPub, init2.ekPub);
    CHECK(bobRoot2 == init2.rootKey);
    CHECK(init2.rootKey != init.rootKey);

    // 签名束被换 → 发起方立刻发现
    auto evil = bob.bundle(1);
    evil.spkPub = cc::crypto::generateX25519().pub;
    CHECK_THROWS(cc::ratchet::x3dhInitiate(alice.ik.priv, alice.ik.pub, evil));
}

TEST_CASE("双棘轮：双向多轮会话，方向切换触发 DH 棘轮") {
    Conversation c;
    for (int round = 1; round <= 5; ++round) {
        const std::string a2b = "A->B 消息 " + std::to_string(round);
        CHECK(c.aliceSays(a2b) == a2b);
        const std::string b2a = "B->A 回复 " + std::to_string(round);
        CHECK(c.bobSays(b2a) == b2a);
    }
    // 同链连发多条也正常
    CHECK(c.aliceSays("连发-1") == "连发-1");
    CHECK(c.aliceSays("连发-2") == "连发-2");
    // Bob 的远端棘轮公钥应随 Alice 方向切换而变化
    const cc::Bytes before = c.br.remoteRatchetPub();
    c.bobSays("触发切换");
    c.aliceSays("新链消息");
    CHECK(before != c.br.remoteRatchetPub());
}

TEST_CASE("双棘轮：乱序到达可补解且互不影响") {
    Conversation c;
    std::vector<cc::ratchet::Envelope> sent;
    for (int i = 0; i < 5; ++i) {
        sent.push_back(c.ar.encrypt("乱序消息 " + std::to_string(i), c.ad));
    }
    cc::Bytes out;
    // 先到第 3 条：缓存 0..2 的密钥
    REQUIRE(c.br.decrypt(sent[3].header, sent[3].ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "乱序消息 3");
    CHECK(c.br.skippedKeys() == 3);
    // 补解 0、2、1（仍乱）
    for (int i : {0, 2, 1}) {
        REQUIRE(c.br.decrypt(sent[i].header, sent[i].ciphertext, c.ad, out) ==
                cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
        CHECK(out == "乱序消息 " + std::to_string(i));
    }
    CHECK(c.br.skippedKeys() == 0);
    // 最后到第 4 条
    REQUIRE(c.br.decrypt(sent[4].header, sent[4].ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "乱序消息 4");
    // Bob 回复不受影响
    CHECK(c.bobSays("乱序之后") == "乱序之后");
}

TEST_CASE("双棘轮：认证失败不破坏状态——篡改包之后原包仍可解密") {
    Conversation c;
    auto env = c.ar.encrypt("机密内容", c.ad);
    cc::Bytes out;
    const auto before = c.br.toJson();  // 序列化基线（M1-02 原子性）

    // 篡改密文体 → 失败，且状态逐字节不变
    cc::Bytes bits = env.ciphertext;
    bits[0] = static_cast<char>(bits[0] ^ 0x40);
    CHECK(c.br.decrypt(env.header, bits, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Failed);
    CHECK(c.br.toJson() == before);

    // 篡改认证标签 → 同样失败且状态不变
    bits = env.ciphertext;
    bits.back() = static_cast<char>(bits.back() ^ 0x01);
    CHECK(c.br.decrypt(env.header, bits, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Failed);
    CHECK(c.br.toJson() == before);

    // AAD 不一致（会话绑定材料不同）→ 失败且状态不变
    CHECK(c.br.decrypt(env.header, env.ciphertext, "wrong-ad", out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Failed);
    CHECK(c.br.toJson() == before);

    // 关键保证：同一条消息的原包此刻仍可正常解密（不再需要重建状态）
    CHECK(c.br.decrypt(env.header, env.ciphertext, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "机密内容");
    CHECK(c.br.toJson() != before);  // 成功才推进
}

TEST_CASE("双棘轮：乱序缓存命中后认证失败，缓存密钥不被消费") {
    Conversation c;
    std::vector<cc::ratchet::Envelope> sent;
    for (int i = 0; i < 3; ++i) {
        sent.push_back(c.ar.encrypt("乱序 " + std::to_string(i), c.ad));
    }
    cc::Bytes out;
    // 先收第 2 条：缓存 0、1
    REQUIRE(c.br.decrypt(sent[2].header, sent[2].ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    REQUIRE(c.br.skippedKeys() == 2);
    const auto before = c.br.toJson();
    // 篡改的第 0 条 → 失败且缓存仍在
    cc::Bytes bits = sent[0].ciphertext;
    bits[0] = static_cast<char>(bits[0] ^ 0x20);
    CHECK(c.br.decrypt(sent[0].header, bits, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Failed);
    CHECK(c.br.skippedKeys() == 2);
    CHECK(c.br.toJson() == before);
    // 原始第 0 条仍可用缓存密钥解开
    CHECK(c.br.decrypt(sent[0].header, sent[0].ciphertext, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "乱序 0");
    CHECK(c.br.skippedKeys() == 1);
}

TEST_CASE("双棘轮：跨 DH 棘轮方向切换后认证失败同样不破坏状态") {
    Conversation c;
    c.bobSays("B->A 建立方向");   // 触发 Bob 首次发送链（Alice 侧 DH 棘轮待命）
    auto next = c.ar.encrypt("新链第一条", c.ad);
    cc::Bytes out;
    REQUIRE(c.br.decrypt(next.header, next.ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    c.bobSays("再切一次");

    auto env = c.ar.encrypt("跨棘轮机密", c.ad);
    const auto before = c.br.toJson();
    cc::Bytes bits = env.ciphertext;
    bits.back() = static_cast<char>(bits.back() ^ 0x02);
    CHECK(c.br.decrypt(env.header, bits, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Failed);
    CHECK(c.br.toJson() == before);
    CHECK(c.br.decrypt(env.header, env.ciphertext, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "跨棘轮机密");
}

TEST_CASE("双棘轮：跳过缓存 64 条边界与 65 条行为明确") {
    Conversation c;
    std::vector<cc::ratchet::Envelope> sent;
    for (int i = 0; i < 65; ++i) {
        sent.push_back(c.ar.encrypt("m" + std::to_string(i), c.ad));
    }
    cc::Bytes out;
    // 第 65 条（序号 64）：需缓存 0..64 共 64 条 = 恰好上限，成功
    CHECK(c.br.decrypt(sent[64].header, sent[64].ciphertext, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(c.br.skippedKeys() == 64);
    // 回补一条后缓存消耗正常
    CHECK(c.br.decrypt(sent[0].header, sent[0].ciphertext, c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(c.br.skippedKeys() == 63);

    Conversation c2;  // 干净会话：直接跳 65 条
    std::vector<cc::ratchet::Envelope> sent2;
    for (int i = 0; i < 66; ++i) {
        sent2.push_back(c2.ar.encrypt("x" + std::to_string(i), c2.ad));
    }
    CHECK(c2.br.decrypt(sent2[65].header, sent2[65].ciphertext, c2.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::TooManySkip);
    // TooManySkip 也不改变状态
    const auto before2 = c2.br.toJson();
    CHECK(c2.br.decrypt(sent2[65].header, sent2[65].ciphertext, c2.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::TooManySkip);
    CHECK(c2.br.toJson() == before2);
}

TEST_CASE("双棘轮：重放同一条消息被拒绝") {
    Conversation c;
    auto env = c.ar.encrypt("只此一条", c.ad);
    cc::Bytes out;
    REQUIRE(c.br.decrypt(env.header, env.ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    // 重放：序号已被消费，既不在缓存也不再派生
    CHECK(c.br.decrypt(env.header, env.ciphertext, c.ad, out) !=
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    // 后续正常消息不受影响
    CHECK(c.bobSays("继续") == "继续");
    CHECK(c.aliceSays("继续-2") == "继续-2");
}

TEST_CASE("双棘轮：前向保密——泄露旧状态解不开新消息") {
    Conversation c;
    c.bobSays("先说一句");        // Bob 已有发送链
    const auto leaked = c.br.toJson();  // 此刻窃取 Bob 全部状态
    auto thief = cc::ratchet::DoubleRatchet::fromJson(leaked);

    // 会话继续推进多轮
    for (int i = 0; i < 3; ++i) {
        c.aliceSays("推进 " + std::to_string(i));
        c.bobSays("回推进 " + std::to_string(i));
    }
    // 窃取者用旧状态解 Alice 的最新消息：必然失败
    auto latest = c.ar.encrypt("最新机密", c.ad);
    cc::Bytes out;
    CHECK(thief.decrypt(latest.header, latest.ciphertext, c.ad, out) !=
          cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    // 真实 Bob 仍可解
    REQUIRE(c.br.decrypt(latest.header, latest.ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "最新机密");
}

TEST_CASE("双棘轮：跳过数超限被拒绝（对端作恶防护）") {
    Conversation c;
    cc::ratchet::Header h;
    h.dhPub = c.ar.remoteRatchetPub();
    h.n = cc::ratchet::kMaxSkip + 10;  // 声称丢了远超上限的序号
    h.pn = 0;
    cc::Bytes out;
    CHECK(c.br.decrypt(h, cc::Bytes(32, 'x'), c.ad, out) ==
          cc::ratchet::DoubleRatchet::DecryptStatus::TooManySkip);
}

TEST_CASE("双棘轮：状态序列化往返后会话无缝继续") {
    Conversation c;
    c.aliceSays("掉线前");
    c.bobSays("掉线前回复");

    // 双端各自落盘再恢复
    auto ar2 = cc::ratchet::DoubleRatchet::fromJson(c.ar.toJson());
    auto br2 = cc::ratchet::DoubleRatchet::fromJson(c.br.toJson());

    auto env = ar2.encrypt("重启后第一条", c.ad);
    cc::Bytes out;
    REQUIRE(br2.decrypt(env.header, env.ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "重启后第一条");
    auto env2 = br2.encrypt("重启后回复", c.ad);
    REQUIRE(ar2.decrypt(env2.header, env2.ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "重启后回复");
    // 旧对象也仍同步（同一状态的另一副本）
    REQUIRE(c.ar.decrypt(env2.header, env2.ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
}

TEST_CASE("信封 JSON 往返与损坏输入") {
    Conversation c;
    auto env = c.ar.encrypt("json 往返", c.ad);
    const auto j = env.toJson();
    auto back = cc::ratchet::Envelope::fromJson(j);
    REQUIRE(back.has_value());
    cc::Bytes out;
    REQUIRE(c.br.decrypt(back->header, back->ciphertext, c.ad, out) ==
            cc::ratchet::DoubleRatchet::DecryptStatus::Ok);
    CHECK(out == "json 往返");

    CHECK_FALSE(cc::ratchet::Envelope::fromJson(nlohmann::json::object())
                    .has_value());
    CHECK_FALSE(
        cc::ratchet::Envelope::fromJson(nlohmann::json::array()).has_value());
}
