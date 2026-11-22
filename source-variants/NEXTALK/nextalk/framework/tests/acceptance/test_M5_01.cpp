#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include <sqlite3.h>
using namespace ccfw;
using namespace cc::proto;
TEST_CASE("M5-01 three real TLS clients use distinct pairwise ciphertext and restore an offline member group") {
    test::RealServer server; test::RealClient alice("alice"), bob("bob"), charlie("charlie");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options()); charlie.registerAndLogin(server.options());
    alice.trust(bob); alice.trust(charlie); bob.trust(alice); charlie.trust(alice); bob.application.shutdown();
    REQUIRE(alice.groups.create("课程设计", {"bob", "charlie"}));
    REQUIRE(test::eventually([&] { alice.application.tick(); charlie.application.tick(); return !alice.groups.snapshots().value().empty() && !charlie.groups.snapshots().value().empty(); }));
    const auto group = alice.groups.snapshots().value().front(); REQUIRE(group.members.size() == 3);
    const std::string secret = "private-group-message-837fa0c2";
    auto report = alice.groups.sendText(group.id, secret); REQUIRE(report); REQUIRE(report.value().members.size() == 2);
    CHECK(report.value().members[0].recipient != "alice"); CHECK(report.value().members[1].recipient != "alice");
    REQUIRE(report.value().members[0].messageId); REQUIRE(report.value().members[1].messageId);
    CHECK(report.value().members[0].messageId != report.value().members[1].messageId);
    REQUIRE(test::eventually([&] { alice.application.tick(); return alice.store.load().value().outbox.size() == 2; }));
    const auto outbox = alice.store.load().value().outbox; CHECK(outbox[0].payloadBase64 != outbox[1].payloadBase64);
    for (const auto& packet : outbox) {
        CHECK(packet.groupId == group.id); cc::Bytes encoded; REQUIRE(cc::base64Decode(packet.payloadBase64, encoded)); CHECK(encoded.find(secret) == std::string::npos);
    }
    for (const auto& member : report.value().members) if (member.recipient == "charlie") alice.settle(charlie, *member.messageId);
    REQUIRE(charlie.store.load().value().history.size() == 1);
    CHECK(charlie.store.load().value().history.front().logicalGroupMessageId == report.value().logicalMessageId);
    CHECK(bob.store.load().value().history.empty()); bob.connect(server.options()); bob.login();
    for (const auto& member : report.value().members) if (member.recipient == "bob") alice.settle(bob, *member.messageId);
    REQUIRE(bob.groups.snapshots().value().size() == 1); CHECK(bob.groups.snapshots().value().front().revision == 1);
    REQUIRE(bob.store.load().value().history.size() == 1); CHECK(bob.store.load().value().history.front().text == secret);
    CHECK(bob.store.load().value().history.front().logicalGroupMessageId == report.value().logicalMessageId);
    CHECK(alice.events.ofType<Error>().empty()); CHECK(bob.events.ofType<Error>().empty()); CHECK(charlie.events.ofType<Error>().empty());
}
TEST_CASE("M5-01 partial failures retry only failed members and persist revisioned snapshots and reports") {
    test::FileClient client("alice");
    struct Chat final : IChatService {
        bool failBob = true; std::vector<SendText> calls;
        Result<std::string> sendText(const SendText& request) override {
            calls.push_back(request);
            if (request.recipient == "bob" && failBob) return Result<std::string>::failure({ErrorCode::StorageFailure, "test", "injected"});
            return Result<std::string>::success(wire::id());
        }
    } chat;
    GroupService groups(chat, client.net, {"file-test", "alice"}, &client.store);
    REQUIRE(groups.applySnapshot({1, 2, "group", {"alice", "bob", "charlie"}}));
    auto stale = groups.applySnapshot({1, 1, "old", {"alice", "bob"}}); REQUIRE_FALSE(stale); CHECK(stale.error().code == ErrorCode::AlreadyProcessed);
    CHECK_FALSE(groups.applySnapshot({1, 2, "conflict", {"alice", "bob", "charlie"}}));
    CHECK_FALSE(groups.applySnapshot({1, 3, "group", {"alice", "bob", "bob"}}));
    CHECK_FALSE(groups.applySnapshot({2, 1, "outsider", {"bob", "charlie"}}));
    auto report = groups.sendText(1, "message"); REQUIRE(report); REQUIRE(chat.calls.size() == 2);
    CHECK_FALSE(report.value().members[0].messageId); REQUIRE(report.value().members[1].messageId);
    const auto successfulId = report.value().members[1].messageId; chat.failBob = false;
    GroupService restored(chat, client.net, {"file-test", "alice"}, &client.store);
    CHECK(restored.snapshots().value().front().revision == 2);
    auto retry = restored.retry(report.value().logicalMessageId); REQUIRE(retry); REQUIRE(chat.calls.size() == 3);
    CHECK(chat.calls.back().recipient == "bob"); CHECK(chat.calls.back().logicalGroupMessageId == report.value().logicalMessageId);
    CHECK(retry.value().members[1].messageId == successfulId); REQUIRE(retry.value().members[0].messageId);
    REQUIRE(restored.retry(report.value().logicalMessageId)); CHECK(chat.calls.size() == 3);
}
TEST_CASE("M5-01 crash between chat commit and group report commit cannot duplicate a recipient message") {
    test::FileClient alice("alice");
    struct FailAfterChat final : IChatService {
        test::FileClient& client;
        explicit FailAfterChat(test::FileClient& client) : client(client) {}
        Result<std::string> sendText(const SendText& request) override { auto result = client.chat.sendText(request); client.store.fail = true; return result; }
    } interrupted(alice);
    GroupService groups(interrupted, alice.net, {"file-test", "alice"}, &alice.store);
    REQUIRE(groups.applySnapshot({1, 1, "group", {"alice", "bob"}})); CHECK_FALSE(groups.sendText(1, "one logical message"));
    alice.store.fail = false; const auto snapshot = alice.store.load().value(); REQUIRE(snapshot.history.size() == 1);
    REQUIRE(snapshot.groupDrafts.size() == 1); CHECK_FALSE(snapshot.groupDrafts.front().report.members.front().messageId);
    GroupService restored(alice.chat, alice.net, {"file-test", "alice"}, &alice.store);
    auto retry = restored.retry(snapshot.groupDrafts.front().report.logicalMessageId); REQUIRE(retry);
    CHECK(retry.value().members.front().messageId == snapshot.history.front().messageId);
    CHECK(alice.store.load().value().history.size() == 1);
}
TEST_CASE("M5-01 real router refuses nonmember senders recipients and malformed groups") {
    cc::server::Storage db(""); cc::server::Router router(db); test::User a, b, m;
    a.save(db, "alice"); b.save(db, "bob"); m.save(db, "mallory"); const auto group = db.createGroup("group", {"alice", "bob"}); REQUIRE(group);
    CHECK_FALSE(db.createGroup("duplicate", {"alice", "alice"})); CHECK_FALSE(db.createGroup("unknown", {"alice", "nobody"}));
    test::Peer alice, bob, mallory; a.login(router, alice, "alice"); b.login(router, bob, "bob"); m.login(router, mallory, "mallory");
    alice.inbox.clear(); bob.inbox.clear(); mallory.inbox.clear();
    auto send = withV2(makeSendMsg("forged", "bob", "chat", cc::base64Encode("ciphertext"), *group)); send.body["from"] = "alice";
    router.onMessage(mallory, send); REQUIRE_FALSE(mallory.inbox.empty()); CHECK(mallory.inbox.back().type == MsgType::Error); CHECK(bob.inbox.empty());
    send.body["to"] = "mallory"; router.onMessage(alice, send); CHECK(alice.inbox.back().type == MsgType::Error);
    auto malformed = withV2(makeCreateGroup("group", {"bob"})); malformed.body["members"].push_back(3);
    router.onMessage(alice, malformed); CHECK(alice.inbox.back().type == MsgType::Error);
    CHECK(db.deliveries().peek("bob", 16).value().empty()); CHECK(db.deliveries().peek("mallory", 16).value().empty());
    send.body["to"] = "bob"; router.onMessage(alice, send); CHECK(alice.inbox.back().type == MsgType::MsgAck); REQUIRE(bob.inbox.size() == 1);
    CHECK(bob.inbox.front().type == MsgType::RecvMsg); CHECK(bodyStr(bob.inbox.front().body, "from") == "alice");
    router.onDisconnect(alice); router.onDisconnect(bob); router.onDisconnect(mallory);
}
