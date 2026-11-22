// M1-03 验收：身份信任与会话建立。
// 用 ILocalStore 测试替身（内存快照）承载已持久化身份，验证：
//   - 未知身份/身份变更 → IdentityChanged，绝不静默建会话；
//   - 只有 acceptPeerIdentity 写入信任记录；
//   - SPK 签名被篡改 → 拒绝且不改任何持久状态；
//   - establish 后双方根密钥一致（有 OPK / 无 OPK 两条路径）。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ccfw/local_store.hpp"
#include "ccfw/session_service.hpp"

#include <stdexcept>

namespace {

using namespace ccfw;
using namespace cc;

// 内存版 ILocalStore：语义与 SQLite 版一致（revision 严格 +1、失败零变化）
class FakeStore final : public ILocalStore {
public:
    Result<> open(const std::filesystem::path&, const AccountScope& scope) override {
        snap_.account = scope;
        opened_ = true;
        return Result<>::success(Unit{});
    }
    Result<ClientSnapshot> load() override {
        if (!opened_) {
            return Result<ClientSnapshot>::failure(
                {ErrorCode::InvalidState, "M1-03", "未打开"});
        }
        return Result<ClientSnapshot>::success(snap_);
    }
    Result<> commit(std::uint64_t expectedRevision,
                    const ClientSnapshot& next) override {
        if (!opened_ || next.revision != expectedRevision + 1 ||
            snap_.revision != expectedRevision) {
            return Result<>::failure(
                {ErrorCode::Conflict, "M1-03", "revision 冲突"});
        }
        snap_ = next;
        return Result<>::success(Unit{});
    }
    void close() noexcept override { opened_ = false; }

    ClientSnapshot snap_;

private:
    bool opened_ = false;
};

struct PeerKeys {
    crypto::X25519KeyPair ik = crypto::generateX25519();
    crypto::Ed25519KeyPair sign = crypto::generateEd25519();
    crypto::X25519KeyPair spk = crypto::generateX25519();
    crypto::X25519KeyPair opk = crypto::generateX25519();
    Bytes spkSig = crypto::ed25519Sign(sign.priv, spk.pub);
};

ratchet::X3dhPeerBundle bundleOf(const PeerKeys& p, bool withOpk) {
    ratchet::X3dhPeerBundle b;
    b.ikPub = p.ik.pub;
    b.spkPub = p.spk.pub;
    b.spkSig = p.spkSig;
    b.signPub = p.sign.pub;
    if (withOpk) {
        b.opkId = 11;
        b.opkPub = p.opk.pub;
    }
    return b;
}

// 已注入本地身份的服务（alice）
struct Harness {
    FakeStore store;
    SessionService service{store};
    PeerKeys bob;

    Harness() {
        AccountScope scope{"unit-test", "alice"};
        REQUIRE(store.open("unit", scope).ok());
        ClientSnapshot snap = store.snap_;
        IdentityState id;
        id.signing = crypto::generateEd25519();
        id.identityDh = crypto::generateX25519();
        id.signedPrekey = crypto::generateX25519();
        id.signedPrekeySignature = crypto::ed25519Sign(id.signing.priv,
                                                       id.signedPrekey.pub);
        snap.identity = id;
        snap.revision = 1;
        REQUIRE(store.commit(0, snap).ok());
    }
};

bool hasSessionFor(const ClientSnapshot& snap, const std::string& peer) {
    for (const auto& s : snap.sessions) {
        if (s.peer == peer) return true;
    }
    return false;
}

bool isTrusted(const ClientSnapshot& snap, const std::string& peer) {
    for (const auto& p : snap.trustedPeers) {
        if (p.username == peer) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("M1-03 未知身份返回 IdentityChanged；显式确认后才能建会话") {
    Harness h;
    const auto bundle = bundleOf(h.bob, true);

    auto r1 = h.service.establish("bob", bundle);
    REQUIRE_FALSE(r1.ok());
    CHECK(r1.error().code == ErrorCode::IdentityChanged);
    CHECK_FALSE(hasSessionFor(h.store.snap_, "bob"));
    CHECK_FALSE(isTrusted(h.store.snap_, "bob"));

    // 显式确认（UI 展示身份后的人工确认入口）
    PeerIdentity identity;
    identity.username = "bob";
    identity.signingPublicKey = h.bob.sign.pub;
    identity.dhPublicKey = h.bob.ik.pub;
    identity.manuallyVerified = true;
    auto r2 = h.service.acceptPeerIdentity(identity);
    REQUIRE(r2.ok());
    CHECK(isTrusted(h.store.snap_, "bob"));

    auto r3 = h.service.establish("bob", bundle);
    REQUIRE(r3.ok());
    CHECK(hasSessionFor(h.store.snap_, "bob"));
}

TEST_CASE("M1-03 有 OPK：响应方按 X3DH 重算的根密钥与发起方入口一致") {
    // establish 内部调用 x3dhInitiate（EK 每次随机，故无法从外部预知）。
    // 一致性断言：对"同一份 EK"，x3dhRespond 输出与 x3dhInitiate 输出相等；
    // establish 已保证它使用的是同一入口与同一束（身份/签名先行核对）。
    Harness h;
    PeerIdentity identity;
    identity.username = "bob";
    identity.signingPublicKey = h.bob.sign.pub;
    identity.dhPublicKey = h.bob.ik.pub;
    REQUIRE(h.service.acceptPeerIdentity(identity).ok());
    const auto bundle = bundleOf(h.bob, true);
    REQUIRE(h.service.establish("bob", bundle).ok());
    CHECK(hasSessionFor(h.store.snap_, "bob"));

    const auto& id = h.store.snap_.identity.value();
    const ratchet::X3dhInitiation probe =
        ratchet::x3dhInitiate(id.identityDh.priv, id.identityDh.pub, bundle);
    const Bytes bobRoot = ratchet::x3dhRespond(
        h.bob.ik.priv, h.bob.spk.priv, h.bob.opk.priv, probe.ikPub,
        probe.ekPub);
    CHECK(bobRoot == probe.rootKey);
    // establish 建立的会话 AD 与公共规则一致
    for (const auto& s : h.store.snap_.sessions) {
        if (s.peer == "bob") {
            const Bytes expectAd =
                id.identityDh.pub < h.bob.ik.pub
                    ? id.identityDh.pub + h.bob.ik.pub
                    : h.bob.ik.pub + id.identityDh.pub;
            CHECK(s.associatedData == expectAd);
        }
    }
}

TEST_CASE("M1-03 无 OPK 路径同样一致") {
    Harness h;
    PeerIdentity identity;
    identity.username = "bob";
    identity.signingPublicKey = h.bob.sign.pub;
    identity.dhPublicKey = h.bob.ik.pub;
    REQUIRE(h.service.acceptPeerIdentity(identity).ok());
    REQUIRE(h.service.establish("bob", bundleOf(h.bob, false)).ok());
    CHECK(hasSessionFor(h.store.snap_, "bob"));

    const auto& id = h.store.snap_.identity.value();
    const auto probe =
        ratchet::x3dhInitiate(id.identityDh.priv, id.identityDh.pub,
                              bundleOf(h.bob, false));
    const Bytes bobRoot = ratchet::x3dhRespond(
        h.bob.ik.priv, h.bob.spk.priv, "", probe.ikPub, probe.ekPub);
    CHECK(bobRoot == probe.rootKey);
}

TEST_CASE("M1-03 篡改 SPK 签名被拒绝且不修改任何持久状态") {
    Harness h;
    PeerIdentity identity;
    identity.username = "bob";
    identity.signingPublicKey = h.bob.sign.pub;
    identity.dhPublicKey = h.bob.ik.pub;
    REQUIRE(h.service.acceptPeerIdentity(identity).ok());
    const auto before = h.store.snap_;

    auto bundle = bundleOf(h.bob, true);
    bundle.spkSig[5] = static_cast<char>(bundle.spkSig[5] ^ 0x40);
    auto r = h.service.establish("bob", bundle);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == ErrorCode::AuthenticationFailed);
    CHECK(h.store.snap_.revision == before.revision);
    CHECK_FALSE(hasSessionFor(h.store.snap_, "bob"));
}

TEST_CASE("M1-03 整体替换束（他人 SPK + 他人签名）与 DH 身份变更均被拒") {
    Harness h;
    PeerKeys attacker;
    PeerIdentity identity;
    identity.username = "bob";
    identity.signingPublicKey = h.bob.sign.pub;
    identity.dhPublicKey = h.bob.ik.pub;
    REQUIRE(h.service.acceptPeerIdentity(identity).ok());
    const auto before = h.store.snap_;

    // 整体替换：攻击者自己的 SPK 与签名（签名本身有效，但身份 DH 不匹配）
    auto replaced = bundleOf(attacker, true);
    CHECK_FALSE(replaced.signPub == h.bob.sign.pub);
    auto r1 = h.service.establish("bob", replaced);
    REQUIRE_FALSE(r1.ok());
    CHECK(r1.error().code == ErrorCode::IdentityChanged);

    // 仅变更 DH 身份（束其余部分仍是 bob 的）
    auto changed = bundleOf(h.bob, true);
    changed.ikPub = attacker.ik.pub;
    auto r2 = h.service.establish("bob", changed);
    REQUIRE_FALSE(r2.ok());
    CHECK(r2.error().code == ErrorCode::IdentityChanged);

    CHECK(h.store.snap_.revision == before.revision);
    CHECK_FALSE(hasSessionFor(h.store.snap_, "bob"));
    // 信任记录未被任何失败路径改写（逐字段核对）
    REQUIRE(h.store.snap_.trustedPeers.size() == before.trustedPeers.size());
    for (std::size_t i = 0; i < before.trustedPeers.size(); ++i) {
        CHECK(h.store.snap_.trustedPeers[i].username ==
              before.trustedPeers[i].username);
        CHECK(h.store.snap_.trustedPeers[i].signingPublicKey ==
              before.trustedPeers[i].signingPublicKey);
        CHECK(h.store.snap_.trustedPeers[i].dhPublicKey ==
              before.trustedPeers[i].dhPublicKey);
    }
}

TEST_CASE("M1-03 显式重新确认可更新身份记录（身份轮换流程）") {
    Harness h;
    PeerIdentity v1;
    v1.username = "bob";
    v1.signingPublicKey = h.bob.sign.pub;
    v1.dhPublicKey = h.bob.ik.pub;
    REQUIRE(h.service.acceptPeerIdentity(v1).ok());

    // 对端轮换身份（新密钥）→ establish 拒绝
    PeerKeys bob2;
    auto r1 = h.service.establish("bob", bundleOf(bob2, true));
    REQUIRE_FALSE(r1.ok());
    CHECK(r1.error().code == ErrorCode::IdentityChanged);

    // 用户显式确认新身份 → establish 成功
    PeerIdentity v2;
    v2.username = "bob";
    v2.signingPublicKey = bob2.sign.pub;
    v2.dhPublicKey = bob2.ik.pub;
    v2.manuallyVerified = true;
    REQUIRE(h.service.acceptPeerIdentity(v2).ok());
    CHECK(h.service.establish("bob", bundleOf(bob2, true)).ok());
}
