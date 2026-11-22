#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "account_fixture.hpp"

using namespace ccfw;
using namespace cc::proto;
TEST_CASE("M3-02 persists identity before registration and logs in after a new client instance") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector;
    const auto path = test::tempDirectory("M3-02") / "client.db";
    cc::Bytes originalIdentity;
    {
        SqliteLocalStore store(protector); REQUIRE(store.open(path, {"server", "alice"}));
        test::RouterTransport net(router); AccountService account(net, store);
        test::negotiate(account, net);
        bool savedBeforeSend = false;
        net.observe = [&](const Message& message) {
            if (message.type == MsgType::Register) savedBeforeSend = store.load().value().identity.has_value();
        };
        REQUIRE(account.registerAccount("alice", "password123"));
        CHECK(savedBeforeSend);
        CHECK_FALSE(account.registerAccount("alice", "password123"));
        REQUIRE(test::pumpAccount(account, net));
        REQUIRE(account.login("alice", "password123"));
        CHECK_FALSE(account.login("alice", "password123"));
        REQUIRE(test::pumpAccount(account, net));
        REQUIRE(account.phase() == AccountPhase::Online);
        REQUIRE(account.uploadPrekeys(8)); REQUIRE(test::pumpAccount(account, net));
        auto saved = store.load(); REQUIRE(saved);
        originalIdentity = saved.value().identity->signing.priv;
        CHECK(saved.value().identity->oneTimePrivateKeys.size() == 8);
        CHECK(db.countOneTimePrekeys(db.findAccount("alice")->id) == 8);
    }
    {
        SqliteLocalStore store(protector); REQUIRE(store.open(path, {"server", "alice"}));
        test::RouterTransport net(router); AccountService account(net, store);
        test::negotiate(account, net);
        REQUIRE(account.login("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
        REQUIRE(account.phase() == AccountPhase::Online);
        const bool same = store.load().value().identity->signing.priv == originalIdentity;
        CHECK(same);
    }
}
TEST_CASE("M3-02 missing identity and wrong account fail before network transmission") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-missing") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router); AccountService account(net, store);
    CHECK_FALSE(account.negotiateProtocol());
    test::negotiate(account, net); const auto sent = net.sent;
    auto missing = account.login("alice", "password123");
    REQUIRE_FALSE(missing); CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK_FALSE(account.registerAccount("bob", "password123"));
    CHECK_FALSE(account.login("bob", "password123"));
    CHECK(net.sent == sent); CHECK_FALSE(store.load().value().identity);
}
TEST_CASE("M3-02 incorrect password leaves a retryable state without changing identity") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-password") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router); AccountService account(net, store);
    test::negotiate(account, net);
    REQUIRE(account.registerAccount("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    const auto identity = store.load().value().identity->signing.priv;
    REQUIRE(account.login("alice", "wrong-password"));
    auto failure = test::pumpAccount(account, net);
    REQUIRE_FALSE(failure); CHECK(failure.error().code == ErrorCode::AuthenticationFailed);
    CHECK(account.phase() == AccountPhase::Connected);
    REQUIRE(account.login("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    CHECK(account.phase() == AccountPhase::Online);
    const bool unchanged = identity == store.load().value().identity->signing.priv; CHECK(unchanged);
}
TEST_CASE("M3-02 malformed challenges and timeouts clear pending authentication") {
    cc::server::Storage db(""); cc::server::Router router(db);
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(test::tempDirectory("M3-02-boundaries") / "client.db", {"server", "alice"}));
    test::RouterTransport net(router); std::int64_t now = 1000;
    AccountService account(net, store, [&] { return now; });
    test::negotiate(account, net);
    REQUIRE(account.registerAccount("alice", "password123")); REQUIRE(test::pumpAccount(account, net));
    REQUIRE(account.login("alice", "password123"));
    auto challenge = net.poll().value().value();
    SUBCASE("too many PBKDF2 iterations") {
        challenge.body["iterations"] = 1000001;
        CHECK_FALSE(account.handle(challenge));
    }
    SUBCASE("wrong nonce length") {
        challenge.body["nonce"] = "YQ==";
        CHECK_FALSE(account.handle(challenge));
    }
    SUBCASE("floating iterations") {
        challenge.body["iterations"] = 200000.0;
        CHECK_FALSE(account.handle(challenge));
    }
    SUBCASE("login success before proving identity") {
        CHECK_FALSE(account.handle(withV2(makeLoginResult(true, ""))));
    }
    SUBCASE("deadline") {
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
