#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "server_fixture.hpp"
#include <sqlite3.h>

using namespace cc::proto;
using namespace ccfw;
using namespace ccfw::server;

TEST_CASE("M2-03 SHA-256 is the standard digest used by delivery receipts") {
    CHECK(cc::base64Encode(cc::crypto::sha256("abc")) == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
}
TEST_CASE("M2-03 durable queue retains ciphertext until matching acknowledgement") {
    const auto path = test::tempDirectory("M2-03") / "queue.db";
    const PendingDelivery message{"bob", "alice", "m1", "ciphertext-marker"};
    {
        SqliteDeliveryQueue queue;
        REQUIRE(queue.open(path.string()));
        REQUIRE(queue.enqueue(message));
        REQUIRE(queue.enqueue(message));
        auto messages = queue.peek("bob", 16);
        REQUIRE(messages);
        REQUIRE(messages.value().size() == 1);
        CHECK(messages.value().front().ciphertextJson == message.ciphertextJson);
        CHECK(queue.peek("bob", 16).value().size() == 1);
        CHECK(queue.acknowledge("mallory", "m1"));
        CHECK(queue.peek("bob", 16).value().size() == 1);
        auto collision = message;
        collision.sender = "mallory";
        auto result = queue.enqueue(collision);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::Conflict);
    }
    {
        SqliteDeliveryQueue queue;
        REQUIRE(queue.open(path.string()));
        REQUIRE(queue.peek("bob", 16).value().size() == 1);
        REQUIRE(queue.acknowledge("bob", "m1"));
        REQUIRE(queue.acknowledge("bob", "m1"));
        CHECK(queue.peek("bob", 16).value().empty());
    }
    {
        SqliteDeliveryQueue queue;
        REQUIRE(queue.open(path.string()));
        const auto retry = queue.enqueue(message);
        REQUIRE_FALSE(retry);
        CHECK(retry.error().code == ErrorCode::AlreadyProcessed);
        auto changed = message;
        changed.ciphertextJson += "changed";
        const auto conflict = queue.enqueue(changed);
        REQUIRE_FALSE(conflict);
        CHECK(conflict.error().code == ErrorCode::Conflict);
        CHECK(queue.peek("bob", 16).value().empty());
    }
}
TEST_CASE("M2-03 failed acknowledgement transaction does not lose ciphertext") {
    const auto path = test::tempDirectory("M2-03-rollback") / "queue.db";
    SqliteDeliveryQueue queue;
    REQUIRE(queue.open(path.string()));
    PendingDelivery message{"bob", "alice", "m1", "ciphertext"};
    REQUIRE(queue.enqueue(message));
    sqlite3* database = nullptr;
    REQUIRE(sqlite3_open(path.string().c_str(), &database) == SQLITE_OK);
    REQUIRE(sqlite3_exec(database,
        "CREATE TRIGGER fail_delete BEFORE DELETE ON pending_deliveries_v2 BEGIN SELECT RAISE(ABORT,'test failure'); END;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK_FALSE(queue.acknowledge("bob", "m1"));
    REQUIRE(queue.peek("bob", 16).value().size() == 1);
    REQUIRE(sqlite3_exec(database, "DROP TRIGGER fail_delete", nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(queue.acknowledge("bob", "m1"));
    CHECK(queue.peek("bob", 16).value().empty());
    sqlite3_close(database);
}
TEST_CASE("M2-03 router retransmits after disconnect and authenticates recipient acknowledgements") {
    cc::server::Storage db("");
    cc::server::Router router(db);
    test::User alice, bob, mallory;
    alice.save(db, "alice"); bob.save(db, "bob"); mallory.save(db, "mallory");
    test::Peer sender, receiver, attacker;
    alice.login(router, sender, "alice");
    const auto wire = withV2(makeSendMsg("m1", "bob", "chat", "Y2lwaGVy"));
    sender.inbox.clear();
    router.onMessage(sender, wire);
    REQUIRE(sender.inbox.size() == 1);
    CHECK(sender.inbox.front().type == MsgType::MsgAck);
    CHECK(bodyBool(sender.inbox.front().body, "accepted"));
    CHECK_FALSE(bodyBool(sender.inbox.front().body, "delivered"));
    bob.login(router, receiver, "bob");
    auto first = receiver.take(MsgType::RecvMsg);
    REQUIRE(first.size() == 1);
    router.onDisconnect(receiver);
    bob.login(router, receiver, "bob");
    auto second = receiver.take(MsgType::RecvMsg);
    REQUIRE(second.size() == 1);
    CHECK(serialize(second.front()) == serialize(first.front()));
    mallory.login(router, attacker, "mallory");
    router.onMessage(attacker, makeRecipientAck("m1", "alice"));
    REQUIRE(db.deliveries().peek("bob", 16).value().size() == 1);
    router.onMessage(receiver, makeRecipientAck("m1", "mallory"));
    REQUIRE(db.deliveries().peek("bob", 16).value().size() == 1);
    sender.inbox.clear();
    router.onMessage(receiver, makeRecipientAck("m1", "alice"));
    REQUIRE(db.deliveries().peek("bob", 16).value().empty());
    auto acknowledgements = sender.take(MsgType::RecipientAck);
    REQUIRE(acknowledgements.size() == 1);
    CHECK(bodyStr(acknowledgements.front().body, "from") == "bob");
    router.onMessage(receiver, makeRecipientAck("m1", "alice"));
    CHECK(sender.inbox.empty());
    router.onMessage(sender, wire); // lost ACK recovered from durable receipt
    auto recovered = sender.take(MsgType::RecipientAck);
    REQUIRE(recovered.size() == 1);
    CHECK(bodyStr(recovered.front().body, "from") == "bob");
    CHECK(db.deliveries().peek("bob", 16).value().empty());
}
TEST_CASE("M2-03 new server instance recovers queued messages and receipt retries") {
    const auto path = test::tempDirectory("M2-03-restart") / "server.db";
    test::User alice, bob;
    const auto message = withV2(makeSendMsg("restart", "bob", "chat", "YQ=="));
    {
        cc::server::Storage db(path.string()); cc::server::Router router(db);
        alice.save(db, "alice"); bob.save(db, "bob");
        test::Peer sender;
        alice.login(router, sender, "alice");
        router.onMessage(sender, message);
    }
    {
        cc::server::Storage db(path.string()); cc::server::Router router(db);
        test::Peer receiver;
        bob.login(router, receiver, "bob");
        REQUIRE(receiver.take(MsgType::RecvMsg).size() == 1);
        router.onMessage(receiver, makeRecipientAck("restart", "alice"));
    }
    {
        cc::server::Storage db(path.string()); cc::server::Router router(db);
        test::Peer sender;
        alice.login(router, sender, "alice");
        router.onMessage(sender, message);
        REQUIRE(sender.take(MsgType::RecipientAck).size() == 1);
    }
}
TEST_CASE("M2-03 version negotiation rejects missing, floating and unsupported versions") {
    cc::server::Storage db(""); cc::server::Router router(db);
    test::Peer peer;
    auto hello = makeHello();
    SUBCASE("unsupported") { hello.body["version"] = 1; }
    SUBCASE("floating") { hello.body["version"] = 2.0; }
    SUBCASE("missing") { hello.body.erase("version"); }
    router.onMessage(peer, hello);
    REQUIRE(peer.inbox.size() == 1);
    CHECK_FALSE(bodyBool(peer.inbox.front().body, "ok"));
    peer.inbox.clear();
    router.onMessage(peer, makeLoginRequest("alice"));
    REQUIRE(peer.inbox.size() == 1);
    CHECK(bodyStr(peer.inbox.front().body, "code") == "protocol_error");
    router.onDisconnect(peer);
    peer.inbox.clear();
    router.onMessage(peer, makeHello());
    REQUIRE(bodyBool(peer.inbox.front().body, "ok"));
    peer.inbox.clear();
    router.onMessage(peer, makeLoginRequest("alice"));
    CHECK(bodyStr(peer.inbox.front().body, "code") == "protocol_error");
}
TEST_CASE("M2-03 send window advances after ACK without duplicate deliveries") {
    cc::server::Storage db(""); cc::server::Router router(db);
    test::User alice, bob;
    alice.save(db, "alice"); bob.save(db, "bob");
    test::Peer sender, receiver;
    alice.login(router, sender, "alice");
    for (int i = 0; i < 70; ++i)
        router.onMessage(sender, withV2(makeSendMsg("bulk-" + std::to_string(i), "bob", "chat", "YQ==")));
    bob.login(router, receiver, "bob");
    auto batch = receiver.take(MsgType::RecvMsg);
    REQUIRE(batch.size() == 32);
    std::size_t received = 0;
    while (!batch.empty()) {
        for (const auto& message : batch) { ++received; router.onMessage(receiver, makeRecipientAck(message.msgId, "alice")); }
        batch = receiver.take(MsgType::RecvMsg);
        REQUIRE(received <= 70);
    }
    CHECK(received == 70);
    CHECK(db.deliveries().peek("bob", 64).value().empty());
}
