#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "application_fixture.hpp"
using namespace ccfw;
using namespace cc::proto;
TEST_CASE("M5-02 typing emits once expires after three seconds and offline clears immediately") {
    test::AppFixture f; f.inject(makePresence("bob", true)); REQUIRE(f.app.tick());
    f.inject(makeTypingFrom("bob")); f.inject(makeTypingFrom("bob")); REQUIRE(f.app.tick());
    REQUIRE(f.sink.events.ofType<TypingEvent>().size() == 1); CHECK(f.sink.events.ofType<TypingEvent>().back().active);
    f.now += 2999; REQUIRE(f.app.tick()); CHECK(f.sink.events.ofType<TypingEvent>().size() == 1);
    f.now += 1; REQUIRE(f.app.tick()); REQUIRE(f.sink.events.ofType<TypingEvent>().size() == 2); CHECK_FALSE(f.sink.events.ofType<TypingEvent>().back().active);
    f.inject(makeTypingFrom("bob")); REQUIRE(f.app.tick()); f.inject(makePresence("bob", false)); REQUIRE(f.app.tick());
    CHECK_FALSE(f.sink.events.ofType<TypingEvent>().back().active);
    const auto count = f.sink.events.ofType<PresenceEvent>().size(); f.inject(makePresence("bob", false)); REQUIRE(f.app.tick());
    CHECK(f.sink.events.ofType<PresenceEvent>().size() == count);
    f.inject(makeTypingFrom("bob")); REQUIRE(f.app.tick()); CHECK_FALSE(f.sink.events.ofType<TypingEvent>().back().active);
}
TEST_CASE("M5-02 outgoing typing is rate limited and reconnect fetches confirmed contacts only") {
    test::AppFixture f; const auto sent = f.net.sent;
    REQUIRE(f.groups.sendTyping("bob")); REQUIRE(f.groups.sendTyping("bob")); CHECK(f.net.sent == sent + 1);
    f.now += 999; REQUIRE(f.groups.sendTyping("bob")); CHECK(f.net.sent == sent + 1);
    f.now += 1; REQUIRE(f.groups.sendTyping("bob")); CHECK(f.net.sent == sent + 2);
    test::User bob; bob.save(f.database, "bob");
    test::User carol; carol.save(f.database, "carol");
    REQUIRE(f.database.requestFriend("alice", "bob") == "pending");
    REQUIRE(f.database.respondFriend("bob", "alice", true));
    f.app.shutdown(); REQUIRE(f.app.connect({})); REQUIRE(f.app.tick()); REQUIRE(f.account.login("alice", "password123")); REQUIRE(f.app.tick());
    REQUIRE_FALSE(f.sink.events.ofType<ContactListEvent>().empty());
    const auto list = f.sink.events.ofType<ContactListEvent>().back().users;
    CHECK(list.size() == 1);
    CHECK(std::find(list.begin(), list.end(), std::make_pair(std::string("bob"), false)) != list.end());
    const auto after = f.net.sent; REQUIRE(f.groups.sendTyping("bob")); CHECK(f.net.sent == after + 1);
}
TEST_CASE("M5-02 actual TLS presence tracks login disconnect and reconnect without restart") {
    test::RealServer server; test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    auto hasPresence = [&](bool online) { const auto events = alice.events.ofType<PresenceEvent>();
        return std::any_of(events.begin(), events.end(), [&](const auto& p) { return p.username == "bob" && p.online == online; }); };
    REQUIRE(test::eventually([&] { alice.application.tick(); return hasPresence(true); }));
    REQUIRE(bob.groups.sendTyping("alice")); REQUIRE(test::eventually([&] { alice.application.tick(); return !alice.events.ofType<TypingEvent>().empty(); }));
    bob.application.shutdown(); REQUIRE(test::eventually([&] { alice.application.tick(); return hasPresence(false); }));
    CHECK_FALSE(alice.events.ofType<TypingEvent>().back().active);
    alice.events.items.clear(); bob.connect(server.options()); bob.login();
    REQUIRE(test::eventually([&] { alice.application.tick(); return hasPresence(true); }));
}
