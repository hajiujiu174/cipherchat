#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "account_fixture.hpp"

using namespace ccfw;
using namespace cc::proto;
// 验证用例“M3-02 registers without private keys and logs in from a fresh local store”所描述的行为。
TEST_CASE("M3-02 registers without private keys and logs in from a fresh local store") {
    cc::server::Storage db("");
    cc::server::Router router(db);
    PlatformSecretProtector protector;
    const auto base = test::tempDirectory("M3-02");
    for (int round = 0; round < 2; ++round) {
        SqliteLocalStore store(protector);
        REQUIRE(store.open(base / (round == 0 ? "original.db" : "fresh.db"), {"server", "alice"}));
        test::RouterTransport net(router);
        AccountService account(net, store);
        test::negotiate(account, net);
        if (!round) {
            REQUIRE(account.registerAccount("alice", "password123"));
            CHECK_FALSE(account.registerAccount("alice", "password123"));
            REQUIRE(test::pumpAccount(account, net));
        }
        CHECK_FALSE(store.load().value().identity);
        auto sent = net.sent;
        net.observe = [](const Message& request) {
            CHECK(request.type == MsgType::LoginRequest);
            CHECK(bodyStr(request.body, "password") == "password123");
        };
        REQUIRE(account.login("alice", "password123"));
        CHECK(account.phase() == AccountPhase::LoginPending);
        CHECK_FALSE(account.login("alice", "password123"));
        REQUIRE(test::pumpAccount(account, net));
        CHECK(account.phase() == AccountPhase::Online);
        CHECK(net.sent == sent + 1);
        REQUIRE(db.findAccount("alice"));
        CHECK(account.userId() == db.findAccount("alice")->id);
        CHECK(db.findAccount("alice")->signPub.empty());
        CHECK(db.findAccount("alice")->signedPrekey.empty());
        net.close();
    }
}
// 验证用例“M3-02 wrong local account does not reach the network”所描述的行为。
TEST_CASE("M3-02 wrong local account does not reach the network") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-missing") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router); AccountService account(net, store);
    CHECK_FALSE(account.negotiateProtocol());
    test::negotiate(account, net); const auto sent = net.sent;
    CHECK_FALSE(account.registerAccount("bob", "password123"));
    CHECK_FALSE(account.login("bob", "password123"));
    CHECK(net.sent == sent);
    REQUIRE(account.login("alice", "password123"));
    REQUIRE_FALSE(test::pumpAccount(account, net));
    CHECK(account.phase() == AccountPhase::Connected);
    CHECK_FALSE(store.load().value().identity);
}
// 验证用例“M3-02 incorrect password leaves a retryable state”所描述的行为。
TEST_CASE("M3-02 incorrect password leaves a retryable state") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-password") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router); AccountService account(net, store);
    test::negotiate(account, net);
    REQUIRE(account.registerAccount("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    REQUIRE(account.login("alice", "wrong-password"));
    auto failure = test::pumpAccount(account, net);
    REQUIRE_FALSE(failure); CHECK(failure.error().code == ErrorCode::AuthenticationFailed);
    CHECK(account.phase() == AccountPhase::Connected);
    REQUIRE(account.login("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    CHECK(account.phase() == AccountPhase::Online);
    CHECK_FALSE(store.load().value().identity);
}
// 验证用例“M3-02 malformed or unsolicited results and timeouts clear pending authentication”所描述的行为。
TEST_CASE("M3-02 malformed or unsolicited results and timeouts clear pending authentication") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-boundaries") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router); std::int64_t now = 1000;
    AccountService account(net, store, [&] { return now; });
    test::negotiate(account, net);
    REQUIRE(account.registerAccount("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    SUBCASE("malformed result") {
        REQUIRE(account.login("alice", "password123"));
        auto result = net.poll().value().value();
        result.body["ok"] = "true";
        CHECK_FALSE(account.handle(result));
    }
    SUBCASE("unsolicited login success") {
        CHECK_FALSE(account.handle(withV2(makeLoginResult(true, "", 1))));
    }
    SUBCASE("deadline") {
        REQUIRE(account.login("alice", "password123"));
        now += 10000;
        auto timed = account.checkTimeout(); REQUIRE_FALSE(timed);
        CHECK(timed.error().code == ErrorCode::Timeout);
    }
    CHECK(account.phase() == AccountPhase::Disconnected);
    CHECK_FALSE(net.connected);
    account.connectionChanged(false); account.connectionChanged(false);
    test::negotiate(account, net);
    REQUIRE(account.login("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    CHECK(account.phase() == AccountPhase::Online);
}

// 旧服务端未声明支持时，客户端不发送密码请求。
TEST_CASE("M3-02 never sends a password to an unsupported server") {
    cc::server::Storage db("");
    cc::server::Router router(db);
    PlatformSecretProtector protector;
    SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-upgrade") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router);
    AccountService account(net, store);
    REQUIRE(net.connect({}));
    account.connectionChanged(true);
    REQUIRE(account.negotiateProtocol());
    auto response = net.poll().value().value();
    response.body.erase("password_login");
    REQUIRE(account.handle(response));
    const auto sent = net.sent;
    auto refused = account.login("alice", "password123");
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::ProtocolError);
    CHECK(net.sent == sent);
    CHECK(net.peer.inbox.empty());
}
