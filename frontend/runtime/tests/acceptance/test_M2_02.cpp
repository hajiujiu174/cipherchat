#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "server_fixture.hpp"

using namespace cc::proto;
using ccfw::test::Peer;
using ccfw::test::User;

namespace {
// 普通密码登录复用同一个协议请求，同时协商现有 v2 聊天模式。
Message loginRequest(std::string_view name, std::string_view password) {
    auto request = withV2(makeLoginRequest(name, password));
    request.body["chat_mode"] = "ordinary-v1";
    return request;
}
} // namespace

// 验证服务器继续使用已有数据库中各账号的盐值和迭代次数。
TEST_CASE("M2-02 verifies an existing account and its bound email in one request") {
    cc::server::Storage database("");
    cc::server::Router router(database);
    const cc::Bytes salt(16, 'x');
    const std::string password = "old-pass123";
    const auto hash = cc::crypto::pbkdf2Sha256(password, salt, 15000, 32);
    const auto id = database.createAccount("alice", hash, salt, 15000, "", "", "", "", "alice@example.test");
    REQUIRE(id);
    Peer peer;
    router.onMessage(peer, makeHello());
    REQUIRE(peer.inbox.size() == 1);
    CHECK(bodyBool(peer.inbox.front().body, "password_login"));
    peer.inbox.clear();
    SUBCASE("username") { router.onMessage(peer, loginRequest("alice", password)); }
    SUBCASE("bound email") { router.onMessage(peer, loginRequest("alice@example.test", password)); }
    REQUIRE(peer.inbox.size() == 1);
    CHECK(peer.inbox.front().type == MsgType::LoginResult);
    CHECK(bodyBool(peer.inbox.front().body, "ok"));
    CHECK(bodyInt(peer.inbox.front().body, "user_id") == *id);
    CHECK(peer.user == "alice");
}

TEST_CASE("M2-02 disconnecting the superseded connection preserves its replacement") {
    cc::server::Storage database("");
    cc::server::Router router(database);
    User alice, bob;
    alice.save(database, "alice");
    bob.save(database, "bob");
    Peer oldPeer, replacement, observer;
    alice.login(router, oldPeer, "alice");
    bob.login(router, observer, "bob");
    alice.login(router, replacement, "alice");
    CHECK(oldPeer.user.empty());
    REQUIRE_FALSE(oldPeer.inbox.empty());
    CHECK(oldPeer.inbox.back().type == MsgType::Error);
    CHECK(bodyStr(oldPeer.inbox.back().body, "code") == "kicked");
    observer.inbox.clear();
    oldPeer.inbox.clear();
    router.onMessage(oldPeer, withV2(makeSendMsg("old-message", "bob", "chat", "YQ==")));
    REQUIRE(oldPeer.inbox.size() == 1);
    CHECK(bodyStr(oldPeer.inbox.front().body, "code") == "not_logged_in");
    CHECK(observer.inbox.empty());
    router.onDisconnect(oldPeer);
    router.onDisconnect(oldPeer);
    CHECK(router.isOnline("alice"));
    CHECK(router.onlineUsers() == std::vector<std::string>{"alice", "bob"});
    CHECK(replacement.user == "alice");
    CHECK(observer.inbox.empty()); // no false offline presence
    replacement.inbox.clear();
    router.onMessage(observer, withV2(makeSendMsg("new-message", "alice", "chat", "YQ==")));
    REQUIRE(replacement.inbox.size() == 1);
    CHECK(replacement.inbox.front().type == MsgType::RecvMsg);
    CHECK(replacement.inbox.front().msgId == "new-message");
    observer.inbox.clear();
    router.onDisconnect(replacement);
    router.onDisconnect(replacement);
    CHECK_FALSE(router.isOnline("alice"));
    REQUIRE(observer.inbox.size() == 1);
    CHECK(observer.inbox.front().type == MsgType::Presence);
    CHECK_FALSE(bodyBool(observer.inbox.front().body, "online"));
}

// 验证用例“M2-02 a cached username alone grants neither routing nor logout authority”所描述的行为。
TEST_CASE("M2-02 a cached username alone grants neither routing nor logout authority") {
    cc::server::Storage database("");
    cc::server::Router router(database);
    User alice;
    alice.save(database, "alice");
    Peer active, stale;
    alice.login(router, active, "alice");
    stale.user = "alice";
    router.onMessage(stale, makeListUsers());
    REQUIRE(stale.inbox.size() == 1);
    CHECK(bodyStr(stale.inbox.front().body, "code") == "not_logged_in");
    router.onDisconnect(stale);
    CHECK(router.isOnline("alice"));
    CHECK(router.onlineUsers() == std::vector<std::string>{"alice"});
}

// 已认证的连接不能借第二次登录留下两个在线账号。
TEST_CASE("M2-02 a logged-in connection cannot retain two account mappings") {
    cc::server::Storage database("");
    cc::server::Router router(database);
    User alice, bob;
    alice.save(database, "alice");
    bob.save(database, "bob");
    Peer peer;
    alice.login(router, peer, "alice");
    peer.inbox.clear();
    router.onMessage(peer, loginRequest("bob", bob.password));
    REQUIRE(peer.inbox.size() == 1);
    CHECK(peer.inbox.front().type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(peer.inbox.front().body, "ok"));
    CHECK(peer.user == "alice");
    CHECK(router.onlineUsers() == std::vector<std::string>{"alice"});
}
