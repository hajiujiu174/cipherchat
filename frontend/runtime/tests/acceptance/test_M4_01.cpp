#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "client_fixture.hpp"
#include "../../src/wire_helpers.hpp"

using namespace ccfw;
// 验证用例“M4-01 real TLS clients deliver ordinary UTF-8 chat exactly once”所描述的行为。
TEST_CASE("M4-01 real TLS clients deliver ordinary UTF-8 chat exactly once") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    const auto first = alice.chat.sendText({"bob", "你好\nNEXTALK", 0, ""});
    REQUIRE(first);
    auto snapshot = alice.store.load().value();
    REQUIRE(snapshot.outbox.size() == 1);
    const auto packet = snapshot.outbox.front();
    alice.settle(bob, first.value());
    const auto second = bob.chat.sendText({"alice", "已收到", 0, ""});
    REQUIRE(second); bob.settle(alice, second.value());
    const auto replay = bob.chat.receive(packet);
    REQUIRE_FALSE(replay);
    CHECK(replay.error().code == ErrorCode::AlreadyProcessed);
    const auto received = bob.store.load().value();
    CHECK(std::count_if(received.history.begin(), received.history.end(), [&](const auto& row) {
        return row.messageId == first.value() && row.text == "你好\nNEXTALK";
    }) == 1);
    CHECK(received.sessions.empty());
}

// 验证用例“M4-01 altered routing and conflicting replay cannot alter received history”所描述的行为。
TEST_CASE("M4-01 altered routing and conflicting replay cannot alter received history") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    auto sent = alice.sessions.encryptAndCommit({"bound-id", "", "alice", "bob", "chat", "bound body", 0, 1});
    REQUIRE(sent);
    auto invalid = sent.value();
    SUBCASE("wrong sender") { invalid.from = "mallory"; }
    SUBCASE("wrong recipient") { invalid.to = "mallory"; }
    SUBCASE("wrong group") { invalid.groupId = 42; }
    SUBCASE("invalid envelope") { invalid.payloadBase64 = "not-base64"; }
    const auto before = bob.store.load().value();
    CHECK_FALSE(bob.sessions.decryptAndCommit(invalid));
    CHECK(bob.store.load().value().revision == before.revision);
    REQUIRE(bob.sessions.decryptAndCommit(sent.value()));
    const auto replay = bob.sessions.decryptAndCommit(sent.value());
    REQUIRE_FALSE(replay);
    CHECK(replay.error().code == ErrorCode::AlreadyProcessed);
    auto different = sent.value();
    different.payloadBase64 = "YQ==";
    const auto conflict = bob.sessions.decryptAndCommit(different);
    REQUIRE_FALSE(conflict);
    CHECK(conflict.error().code == ErrorCode::Conflict);
    CHECK(bob.store.load().value().history.size() == 1);
}
// 验证用例“M4-01 failed local commit never sends and disconnected send keeps identical payload”所描述的行为。
TEST_CASE("M4-01 failed local commit never sends and disconnected send keeps identical payload") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
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
    CHECK(retransmitted.outbox.front().messageId == offline.outbox.front().messageId);
    alice.settle(bob, queued.value());
    CHECK(bob.store.load().value().history.size() == 2);
}
// 验证用例“M4-01 simultaneous first sends reach both clients”所描述的行为。
TEST_CASE("M4-01 simultaneous first sends reach both clients") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    auto a = alice.chat.sendText({"bob", "from alice", 0, ""}); REQUIRE(a);
    auto b = bob.chat.sendText({"alice", "from bob", 0, ""}); REQUIRE(b);
    alice.settle(bob, a.value()); bob.settle(alice, b.value());
    auto nextA = alice.chat.sendText({"bob", "next alice", 0, ""}); REQUIRE(nextA); alice.settle(bob, nextA.value());
    auto nextB = bob.chat.sendText({"alice", "next bob", 0, ""}); REQUIRE(nextB); bob.settle(alice, nextB.value());
    CHECK(alice.store.load().value().history.size() == 4);
    CHECK(bob.store.load().value().history.size() == 4);
}
// 验证用例“M4-01 out-of-order delivery and receive commit rollback”所描述的行为。
TEST_CASE("M4-01 out-of-order delivery and receive commit rollback") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
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
// 验证用例“M4-01 server durable queue stores the ordinary wire payload and delivers it after login”所描述的行为。
TEST_CASE("M4-01 server durable queue stores the ordinary wire payload and delivers it after login") {
    test::RealServer server;
    test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    bob.application.shutdown();
    const std::string secret = "unique-private-chat-content-9ca8d94b";
    auto sent = alice.chat.sendText({"bob", secret, 0, ""}); REQUIRE(sent);
    REQUIRE(test::eventually([&] {
        alice.application.tick();
        const auto snapshot = alice.store.load().value();
        return !snapshot.history.empty() && snapshot.history.back().state == DeliveryState::ServerAccepted;
    }));
    const auto encodedPayload = alice.store.load().value().outbox.front().payloadBase64;
    cc::Bytes wirePayload; REQUIRE(cc::base64Decode(encodedPayload, wirePayload));
    const auto body = nlohmann::json::parse(wirePayload);
    CHECK(body.at("chat_mode") == "ordinary-v1");
    cc::Bytes decoded;
    REQUIRE(cc::base64Decode(body.at("body_base64").get<std::string>(), decoded));
    CHECK(decoded == secret);
    bool foundPayload = false;
    for (const auto& entry : std::filesystem::directory_iterator(server.directory)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file), {}};
        CHECK(bytes.find(secret) == std::string::npos);
        foundPayload = foundPayload || bytes.find(encodedPayload) != std::string::npos;
    }
    CHECK(foundPayload);
    bob.connect(server.options()); bob.login(); alice.settle(bob, sent.value());
}
