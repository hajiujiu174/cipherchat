#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "client_fixture.hpp"
#include "../../src/wire_helpers.hpp"

using namespace ccfw;
TEST_CASE("M4-01 real TLS clients establish trust and receive encrypted UTF-8 chat exactly once") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    auto message = alice.chat.sendText({"bob", "端到端测试 private-unique-message", 0, ""}); REQUIRE(message);
    REQUIRE(test::eventually([&] {
        alice.application.tick(); bob.application.tick();
        return !alice.events.ofType<PeerIdentity>().empty();
    }));
    CHECK(bob.store.load().value().history.empty());
    auto identity = alice.events.ofType<PeerIdentity>().back(); identity.manuallyVerified = true;
    REQUIRE(alice.sessions.acceptPeerIdentity(identity)); REQUIRE(alice.chat.retryPending("bob"));
    REQUIRE(test::eventually([&] {
        alice.application.tick(); bob.application.tick();
        return !bob.events.ofType<PeerIdentity>().empty();
    }));
    identity = bob.events.ofType<PeerIdentity>().back(); identity.manuallyVerified = true;
    REQUIRE(bob.sessions.acceptPeerIdentity(identity)); REQUIRE(bob.chat.retryPending("alice"));
    alice.settle(bob, message.value());
    const auto history = bob.store.load().value().history;
    REQUIRE(history.size() == 1); CHECK(history.front().text == "端到端测试 private-unique-message");
    auto reply = bob.chat.sendText({"alice", "已收到", 0, ""}); REQUIRE(reply);
    bob.settle(alice, reply.value());
    CHECK(alice.store.load().value().history.size() == 2);
}
TEST_CASE("M4-01 altered routing and ciphertext leave ratchet and OPK unchanged") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    alice.trust(bob); bob.trust(alice);
    const auto bobIdentity = *bob.store.load().value().identity;
    auto opk = cc::crypto::generateX25519();
    auto bobSnapshot = bob.store.load().value();
    const auto keyId = bobSnapshot.identity->nextPrekeyId++;
    bobSnapshot.identity->oneTimePrivateKeys[keyId] = opk.priv;
    const auto expected = bobSnapshot.revision++; REQUIRE(bob.store.commit(expected, bobSnapshot));
    cc::ratchet::X3dhPeerBundle bundle{bobIdentity.identityDh.pub, bobIdentity.signedPrekey.pub,
        bobIdentity.signedPrekeySignature, bobIdentity.signing.pub, keyId, opk.pub};
    REQUIRE(alice.sessions.establish("bob", bundle));
    PlainEnvelope plain{wire::id(), "", "alice", "bob", "chat", "secret", 0, 123};
    auto encrypted = alice.sessions.encryptAndCommit(plain); REQUIRE(encrypted);
    auto invalid = encrypted.value();
    SUBCASE("group") { invalid.groupId = 7; }
    SUBCASE("recipient") { invalid.to = "mallory"; }
    SUBCASE("kind") { invalid.kind = "file_meta"; }
    SUBCASE("id") { invalid.messageId = "changed"; }
    SUBCASE("ciphertext") {
        cc::Bytes bytes; REQUIRE(cc::base64Decode(invalid.payloadBase64, bytes));
        auto encoded = nlohmann::json::parse(bytes);
        cc::Bytes cipher; REQUIRE(cc::base64Decode(encoded["ct"].get<std::string>(), cipher)); cipher[0] ^= 1;
        encoded["ct"] = cc::base64Encode(cipher); invalid.payloadBase64 = cc::base64Encode(encoded.dump());
    }
    const auto before = nlohmann::json(bob.store.load().value());
    CHECK_FALSE(bob.sessions.decryptAndCommit(invalid));
    const bool unchanged = before == nlohmann::json(bob.store.load().value()); CHECK(unchanged);
    auto received = bob.sessions.decryptAndCommit(encrypted.value()); REQUIRE(received); CHECK(received.value().plaintext == "secret");
    CHECK_FALSE(bob.store.load().value().identity->oneTimePrivateKeys.count(keyId));
    auto duplicate = bob.sessions.decryptAndCommit(encrypted.value()); REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().code == ErrorCode::AlreadyProcessed);
    CHECK(bob.store.load().value().history.size() == 1);
}
TEST_CASE("M4-01 failed local commit never sends and disconnected send keeps identical ciphertext") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    alice.trust(bob); bob.trust(alice);
    auto id = alice.chat.sendText({"bob", "establish", 0, ""}); REQUIRE(id); alice.settle(bob, id.value());
    const auto before = nlohmann::json(alice.store.load().value());
    alice.store.fail = true;
    CHECK_FALSE(alice.chat.sendText({"bob", "must-not-send", 0, ""}));
    const bool unchanged = nlohmann::json(alice.store.load().value()) == before; CHECK(unchanged);
    alice.store.fail = false; alice.application.shutdown();
    auto queued = alice.chat.sendText({"bob", "saved while disconnected", 0, ""}); REQUIRE(queued);
    const auto offline = alice.store.load().value(); REQUIRE(offline.outbox.size() == 1);
    auto received = bob.sessions.decryptAndCommit(offline.outbox.front()); REQUIRE(received);
    bob.store.fail = true;
    CHECK_FALSE(bob.sessions.encryptAndCommit({wire::id(), "", "bob", "alice", "chat", "rollback", 0, 123}));
    bob.store.fail = false;
    alice.connect(server.options()); alice.login();
    REQUIRE(alice.chat.flushOutbox());
    const auto retransmitted = alice.store.load().value(); REQUIRE(retransmitted.outbox.size() == 1);
    CHECK(retransmitted.outbox.front().payloadBase64 == offline.outbox.front().payloadBase64);
    const bool ratchetSame = retransmitted.sessions.front().ratchetState == offline.sessions.front().ratchetState; CHECK(ratchetSame);
    alice.settle(bob, queued.value());
    CHECK(bob.store.load().value().history.size() == 2);
}
TEST_CASE("M4-01 simultaneous first sends remain decryptable") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    alice.trust(bob); bob.trust(alice);
    auto a = alice.chat.sendText({"bob", "from alice", 0, ""}); REQUIRE(a);
    auto b = bob.chat.sendText({"alice", "from bob", 0, ""}); REQUIRE(b);
    alice.settle(bob, a.value()); bob.settle(alice, b.value());
    auto nextA = alice.chat.sendText({"bob", "next alice", 0, ""}); REQUIRE(nextA); alice.settle(bob, nextA.value());
    auto nextB = bob.chat.sendText({"alice", "next bob", 0, ""}); REQUIRE(nextB); bob.settle(alice, nextB.value());
    CHECK(alice.store.load().value().history.size() == 4);
    CHECK(bob.store.load().value().history.size() == 4);
}
TEST_CASE("M4-01 reordered first-chain messages and receive commit rollback preserve keys") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    alice.trust(bob); bob.trust(alice);
    const auto identity = *bob.store.load().value().identity;
    REQUIRE(alice.sessions.establish("bob", {identity.identityDh.pub, identity.signedPrekey.pub,
        identity.signedPrekeySignature, identity.signing.pub, std::nullopt, {}}));
    std::vector<WireEnvelope> packets;
    for (int i = 0; i < 3; ++i) {
        auto packet = alice.sessions.encryptAndCommit({wire::id(), "", "alice", "bob", "chat", std::to_string(i), 0, 1});
        REQUIRE(packet); packets.push_back(packet.value());
    }
    const auto before = nlohmann::json(bob.store.load().value());
    bob.store.fail = true;
    auto failed = bob.chat.receive(packets[2]); REQUIRE_FALSE(failed);
    CHECK(failed.error().code == ErrorCode::StorageFailure);
    const bool unchanged = before == nlohmann::json(bob.store.load().value()); CHECK(unchanged);
    CHECK(bob.chat.takeEvents().empty());
    bob.store.fail = false;
    for (int index : {2, 0, 1}) {
        auto received = bob.chat.receive(packets[index]); REQUIRE(received);
        CHECK(received.value().text == std::to_string(index));
    }
    CHECK(bob.store.load().value().history.size() == 3);
}
TEST_CASE("M4-01 server durable queue and database pages contain only encrypted chat payload") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    alice.trust(bob); bob.trust(alice); bob.application.shutdown();
    const std::string secret = "unique-private-chat-content-9ca8d94b";
    auto sent = alice.chat.sendText({"bob", secret, 0, ""}); REQUIRE(sent);
    REQUIRE(test::eventually([&] {
        alice.application.tick();
        const auto snapshot = alice.store.load().value();
        return !snapshot.history.empty() && snapshot.history.back().state == DeliveryState::ServerAccepted;
    }));
    const auto ciphertext = alice.store.load().value().outbox.front().payloadBase64;
    cc::Bytes wirePayload; REQUIRE(cc::base64Decode(ciphertext, wirePayload));
    CHECK(wirePayload.find(secret) == std::string::npos);
    bool foundCiphertext = false;
    for (const auto& entry : std::filesystem::directory_iterator(server.directory)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file), {}};
        CHECK(bytes.find(secret) == std::string::npos);
        foundCiphertext = foundCiphertext || bytes.find(ciphertext) != std::string::npos;
    }
    CHECK(foundCiphertext);
    bob.connect(server.options()); bob.login(); alice.settle(bob, sent.value());
}
