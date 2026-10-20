#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "application_fixture.hpp"
using namespace ccfw;
using namespace cc::proto;

TEST_CASE("M3-05 queue delivery processes sixty-four messages per tick on the business thread") {
    test::AppFixture f; const auto owner = std::this_thread::get_id();
    std::thread producer([&] { for (int i = 0; i < 70; ++i) f.inject(makePresence("user" + std::to_string(i), true)); }); producer.join();
    CHECK(f.sink.events.items.empty()); const auto before = f.net.polls;
    REQUIRE(f.app.tick()); CHECK(f.net.polls - before == 64); CHECK(f.net.peer.inbox.size() == 6);
    CHECK(f.sink.events.ofType<PresenceEvent>().size() == 64); REQUIRE(f.app.tick()); CHECK(f.net.peer.inbox.empty());
    CHECK(f.sink.events.ofType<PresenceEvent>().size() == 70);
    for (auto caller : f.sink.callers) CHECK(caller == owner);
    for (auto caller : f.store.calls) CHECK(caller == owner);
}
TEST_CASE("M3-05 routes account text file and group events and rejects unknown messages") {
    test::AppFixture f;
    f.inject(makeGroupUpdate(1, "group", {"alice", "bob"})); REQUIRE(f.app.tick()); CHECK(f.sink.events.ofType<GroupSnapshot>().size() == 1);
    REQUIRE(f.groups.snapshots()); CHECK(f.groups.snapshots().value().size() == 1);
    const auto prior = f.sink.events.ofType<GroupSnapshot>().size(); f.inject(makeGroupUpdate(1, "group", {"alice", "bob"}));
    REQUIRE(f.app.tick()); CHECK(f.sink.events.ofType<GroupSnapshot>().size() == prior);
    for (const auto* kind : {"chat", "file_meta", "file_ack"}) f.inject(makeRecvMsg(std::string("id-") + kind, "bob", "alice", kind, "e30=", 0, 0));
    f.inject(makeRecvMsg("chunk", "bob", "alice", "file_chunk", "e30=", 0, 0));
    f.inject(makeRecvMsg("unknown", "bob", "alice", "unknown", "e30=", 0, 0));
    CHECK_FALSE(f.app.tick());
    for (const auto* kind : {"chat", "file_meta", "file_ack"}) CHECK(std::find(f.sessions.decryptedKinds.begin(), f.sessions.decryptedKinds.end(), kind) != f.sessions.decryptedKinds.end());
    CHECK_FALSE(f.sink.events.ofType<Error>().empty()); CHECK(f.store.load().value().history.empty());
    f.inject(makeHello()); CHECK_FALSE(f.app.tick());
}
TEST_CASE("M3-05 transport failure disconnects and shutdown is idempotent") {
    test::AppFixture f; f.inject(makePresence("bob", true)); REQUIRE(f.app.tick()); f.net.connected = false;
    CHECK_FALSE(f.app.tick()); CHECK(f.account.phase() == AccountPhase::Disconnected);
    REQUIRE_FALSE(f.sink.events.ofType<UserListEvent>().empty()); CHECK(f.sink.events.ofType<UserListEvent>().back().users.empty());
    const auto events = f.sink.events.items.size(); f.app.shutdown(); f.app.shutdown(); CHECK(f.sink.events.items.size() == events);
}
