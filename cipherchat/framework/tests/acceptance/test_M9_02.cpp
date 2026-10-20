#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "application_fixture.hpp"
using namespace cc::proto;
using namespace cc::server;
using namespace ccfw;
namespace {
struct Logged {
    test::User alice, bob, carol;
    test::Peer a, b, c;
    Storage db{""}; Router router{db};
    Logged() {
        alice.save(db, "alice"); bob.save(db, "bob"); carol.save(db, "carol");
        alice.login(router, a, "alice"); bob.login(router, b, "bob"); carol.login(router, c, "carol");
        router.onMessage(a, withV2(makeListContacts())); router.onMessage(b, withV2(makeListContacts()));
        a.inbox.clear(); b.inbox.clear(); c.inbox.clear();
    }
};
std::vector<std::string> names(const Message& message) {
    std::vector<std::string> result; for (const auto& item : message.body.at("users")) result.push_back(item.at("username").get<std::string>()); return result;
}
}
TEST_CASE("M9-02 new accounts have empty contacts and authenticated paged discovery") {
    Logged f;
    f.router.onMessage(f.a, withV2(makeListContacts()));
    REQUIRE(f.a.inbox.size() == 1); CHECK(names(f.a.inbox.front()).empty());
    f.a.inbox.clear(); auto query = withV2(makeSearchUsers("")); query.msgId = "search-one";
    f.router.onMessage(f.a, query);
    REQUIRE(f.a.inbox.size() == 1); CHECK(f.a.inbox.front().msgId == "search-one");
    CHECK(names(f.a.inbox.front()) == std::vector<std::string>{"bob", "carol"});
    f.a.inbox.clear(); query.body["query"] = 12; f.router.onMessage(f.a, query); CHECK(f.a.inbox.back().type == MsgType::Error);
    test::Peer anonymous; f.router.onMessage(anonymous, makeListContacts()); CHECK(anonymous.inbox.back().type == MsgType::Error);
    f.router.onMessage(anonymous, makeAddFriend("alice")); CHECK(f.db.friendRequests("alice", true).empty());
}
TEST_CASE("M9-02 requests require recipient consent; rejection, forgery and legacy push") {
    Logged f;
    f.router.onMessage(f.a, withV2(makeAddFriend("bob")));
    CHECK(f.db.friendsForUser("alice").empty()); CHECK(f.db.friendsForUser("bob").empty());
    CHECK(f.db.friendRequests("bob", true) == std::vector<std::string>{"alice"});
    REQUIRE(f.b.inbox.size() == 1); CHECK(f.b.inbox.back().body.at("incoming") == nlohmann::json::array({"alice"}));
    f.router.onMessage(f.a, withV2(makeAddFriend("bob"))); CHECK(f.db.friendRequests("bob", true).size() == 1);
    f.router.onMessage(f.c, withV2(makeRespondFriend("alice", true))); CHECK_FALSE(bodyBool(f.c.inbox.back().body, "ok"));
    f.router.onMessage(f.a, withV2(makeRespondFriend("bob", true))); CHECK(f.db.friendsForUser("alice").empty());
    f.router.onMessage(f.b, withV2(makeRespondFriend("alice", false))); CHECK(f.db.friendRequests("bob", true).empty());
    CHECK(f.db.friendsForUser("alice").empty());
    f.router.onMessage(f.a, withV2(makeAddFriend("bob")));
    f.router.onMessage(f.b, withV2(makeRespondFriend("alice", true)));
    CHECK(f.db.friendsForUser("alice") == std::vector<std::string>{"bob"}); CHECK(f.db.friendsForUser("bob") == std::vector<std::string>{"alice"});
    f.router.onMessage(f.a, withV2(makeSearchUsers(""))); CHECK(names(f.a.inbox.back()) == std::vector<std::string>{"carol"});
    f.c.inbox.clear(); f.router.onMessage(f.a, withV2(makeAddFriend("carol"))); CHECK(f.c.inbox.empty());
    f.router.onMessage(f.c, withV2(makeListContacts())); CHECK(f.c.inbox.back().body.at("incoming") == nlohmann::json::array({"alice"}));
    f.router.onMessage(f.a, withV2(makeAddFriend("alice"))); CHECK_FALSE(bodyBool(f.a.inbox.back().body, "ok"));
}
TEST_CASE("M9-02 requests and bilateral friendships survive database destruction and reopening") {
    const auto path = (test::tempDirectory("M9-02-persist") / "server.db").string();
    {
        Storage db(path); test::User user; user.save(db, "alice"); user.save(db, "bob");
        CHECK(db.requestFriend("alice", "bob") == "pending");
    }
    {
        Storage db(path); CHECK(db.friendRequests("bob", true) == std::vector<std::string>{"alice"});
        CHECK(db.friendsForUser("bob").empty()); REQUIRE(db.respondFriend("bob", "alice", true));
    }
    {
        Storage db(path); CHECK(db.friendsForUser("alice") == std::vector<std::string>{"bob"});
        CHECK(db.friendsForUser("bob") == std::vector<std::string>{"alice"}); CHECK(db.friendRequests("bob", true).empty());
    }
}
TEST_CASE("M9-02 all users are discoverable beyond 256 with stable cursors") {
    Logged f; test::User user; for (int i = 0; i < 270; ++i) user.save(f.db, "user_" + std::to_string(i));
    std::set<std::string> seen; std::string after;
    do {
        f.a.inbox.clear(); f.router.onMessage(f.a, withV2(makeSearchUsers("", after)));
        const auto& page = f.a.inbox.back(); const auto users = names(page); CHECK(users.size() <= 50);
        for (const auto& name : users) CHECK(seen.insert(name).second);
        after = bodyStr(page.body, "next");
    } while (!after.empty());
    CHECK(seen.size() == 272);
    f.a.inbox.clear(); f.router.onMessage(f.a, withV2(makeSearchUsers("USER_269"))); CHECK(names(f.a.inbox.back()) == std::vector<std::string>{"user_269"});
    f.a.inbox.clear(); f.router.onMessage(f.a, withV2(makeSearchUsers("%"))); CHECK(names(f.a.inbox.back()).empty());
}
TEST_CASE("M9-02 actual TLS temporary encrypted chat and offline friend request") {
    test::RealServer server; test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options());
    alice.trust(bob); bob.trust(alice);
    auto sent = alice.chat.sendText({"bob", "temporary encrypted conversation", 0, ""}); REQUIRE(sent); alice.settle(bob, sent.value());
    REQUIRE(alice.application.listContacts()); REQUIRE(bob.application.listContacts());
    REQUIRE(test::eventually([&] { alice.application.tick(); bob.application.tick(); return !alice.events.ofType<ContactListEvent>().empty() && !bob.events.ofType<ContactListEvent>().empty(); }));
    CHECK(alice.events.ofType<ContactListEvent>().back().users.empty()); CHECK(bob.events.ofType<ContactListEvent>().back().users.empty());
    bob.application.shutdown();
    REQUIRE(alice.application.addFriend("bob"));
    REQUIRE(test::eventually([&] { alice.application.tick(); return !alice.events.ofType<FriendResultEvent>().empty(); }));
    CHECK(alice.events.ofType<FriendResultEvent>().back().state == "pending");
    bob.events.items.clear(); bob.connect(server.options()); bob.login();
    REQUIRE(test::eventually([&] { bob.application.tick(); const auto list = bob.events.ofType<ContactListEvent>(); return !list.empty() && list.back().incoming == std::vector<std::string>{"alice"}; }));
    REQUIRE(bob.application.respondFriend("alice", true));
    REQUIRE(test::eventually([&] { alice.application.tick(); bob.application.tick(); return alice.events.ofType<ContactListEvent>().back().users.size() == 1 && bob.events.ofType<ContactListEvent>().back().users.size() == 1; }));
}
TEST_CASE("M9-02 application discards superseded searches and never requests full users on login") {
    test::AppFixture f;
    REQUIRE(f.app.searchUsers("old")); REQUIRE(f.app.searchUsers("alice")); REQUIRE(f.app.tick());
    const auto events = f.sink.events.ofType<StrangerListEvent>(); REQUIRE(events.size() == 1); CHECK(events.back().query == "alice");
    f.sink.events.items.clear(); f.app.shutdown(); REQUIRE(f.app.connect({})); REQUIRE(f.app.tick());
    REQUIRE(f.account.login("alice", "password123")); REQUIRE(f.app.tick());
    CHECK(f.sink.events.ofType<UserListEvent>().empty()); CHECK_FALSE(f.sink.events.ofType<ContactListEvent>().empty());
}
