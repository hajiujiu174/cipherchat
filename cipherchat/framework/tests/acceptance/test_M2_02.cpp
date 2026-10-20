// M2-02: exercise the real Router, SQLite store and shared login cryptography.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ccfw/server_services.hpp"
#include "common/auth/login_proof.hpp"
#include "common/crypto/crypto.hpp"
#include "server/router/router.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {
using namespace cc;
using namespace cc::proto;

struct TestPeer final : cc::server::Peer {
    std::string user;
    std::vector<Message> inbox;
    void deliver(const Message& message) override { inbox.push_back(message); }
    const std::string& username() const override { return user; }
    void setUsername(std::string name) override { user = std::move(name); }
};

struct Identity {
    cc::crypto::Ed25519KeyPair signing = cc::crypto::generateEd25519();
    cc::crypto::X25519KeyPair dh = cc::crypto::generateX25519();
    cc::crypto::X25519KeyPair spk = cc::crypto::generateX25519();
    Bytes salt = cc::crypto::randomBytes(cc::auth::kPbkdf2SaltLen);
    Bytes passwordKey = cc::crypto::pbkdf2Sha256(
        "test-password", salt, cc::auth::kPbkdf2Iterations, cc::auth::kPbkdf2KeyLen);

    void save(cc::server::Storage& database, const std::string& username) const {
        REQUIRE(database.createAccount(username, passwordKey, salt,
            cc::auth::kPbkdf2Iterations, base64Encode(signing.pub),
            base64Encode(dh.pub), base64Encode(spk.pub),
            base64Encode(cc::crypto::ed25519Sign(signing.priv, spk.pub))).has_value());
    }

    Message response(const Message& challenge, const std::string& username) const {
        Bytes nonce;
        REQUIRE(base64Decode(bodyStr(challenge.body, "nonce"), nonce));
        REQUIRE(nonce.size() == cc::auth::kLoginNonceLen);
        return makeLoginResponse(username,
            base64Encode(cc::auth::deriveLoginProof(passwordKey, nonce)),
            base64Encode(nonce),
            base64Encode(cc::crypto::ed25519Sign(signing.priv, nonce)));
    }
};

struct Server {
    std::int64_t nowMs = 1'000;
    cc::server::Storage database{""};
    cc::server::Router router{database, [this] { return nowMs; }};

    Message challenge(TestPeer& peer, const std::string& username) {
        peer.inbox.clear();
        router.onMessage(peer, makeLoginRequest(username));
        REQUIRE(peer.inbox.size() == 1);
        REQUIRE(peer.inbox.front().type == MsgType::LoginChallenge);
        return peer.inbox.front();
    }

    bool respond(TestPeer& peer, const Message& response) {
        peer.inbox.clear();
        router.onMessage(peer, response);
        REQUIRE_FALSE(peer.inbox.empty());
        REQUIRE(peer.inbox.front().type == MsgType::LoginResult);
        return bodyBool(peer.inbox.front().body, "ok");
    }

    void login(TestPeer& peer, const std::string& username, const Identity& identity) {
        const auto request = challenge(peer, username);
        REQUIRE(respond(peer, identity.response(request, username)));
    }
};
} // namespace

TEST_CASE("M2-02 challenge book validates fields and consumes only the bound connection") {
    using namespace ccfw::server;
    ChallengeBook book;
    Challenge challenge{"connection-a", "alice", Bytes(cc::auth::kLoginNonceLen, 'a'), 2'000};
    SUBCASE("empty connection") { challenge.connectionId.clear(); }
    SUBCASE("empty account") { challenge.username.clear(); }
    SUBCASE("short nonce") { challenge.nonce.pop_back(); }
    SUBCASE("long nonce") { challenge.nonce.push_back('a'); }
    auto invalid = book.issue(challenge);
    REQUIRE_FALSE(invalid.ok());
    CHECK(invalid.error().code == ccfw::ErrorCode::InvalidArgument);
    challenge = {"connection-a", "alice", Bytes(cc::auth::kLoginNonceLen, 'a'), 2'000};
    REQUIRE(book.issue(challenge).ok());
    auto wrongConnection = book.consume("connection-b", 1'000);
    REQUIRE_FALSE(wrongConnection.ok());
    CHECK(wrongConnection.error().code == ccfw::ErrorCode::NotFound);
    auto consumed = book.consume("connection-a", 1'999);
    REQUIRE(consumed.ok());
    CHECK(consumed.value().connectionId == challenge.connectionId);
    CHECK(consumed.value().username == challenge.username);
    CHECK(consumed.value().nonce == challenge.nonce);
    CHECK(consumed.value().expiresAtMs == challenge.expiresAtMs);
    CHECK_FALSE(book.consume("connection-a", 1'999).ok());
}

TEST_CASE("M2-02 expired challenges and disconnected challenges are consumed permanently") {
    using namespace ccfw::server;
    ChallengeBook book;
    const Challenge challenge{"connection", "alice", Bytes(cc::auth::kLoginNonceLen, 'a'), 2'000};
    REQUIRE(book.issue(challenge).ok());
    auto expired = book.consume("connection", 2'000);
    REQUIRE_FALSE(expired.ok());
    CHECK(expired.error().code == ccfw::ErrorCode::Timeout);
    auto replay = book.consume("connection", 2'000);
    REQUIRE_FALSE(replay.ok());
    CHECK(replay.error().code == ccfw::ErrorCode::NotFound);
    REQUIRE(book.issue(challenge).ok());
    book.disconnect("connection");
    book.disconnect("connection");
    CHECK_FALSE(book.consume("connection", 1'000).ok());
}

TEST_CASE("M2-02 challenge capacity allows replacement and reclaims expired records") {
    using namespace ccfw::server;
    ChallengeBook book;
    for (std::size_t i = 0; i < kMaxPendingAuth; ++i) {
        REQUIRE(book.issue({std::to_string(i), "alice", Bytes(cc::auth::kLoginNonceLen, 'a'), 2'000}).ok());
    }
    Challenge extra{"extra", "alice", Bytes(cc::auth::kLoginNonceLen, 'b'), 3'000};
    auto full = book.issue(extra);
    REQUIRE_FALSE(full.ok());
    CHECK(full.error().code == ccfw::ErrorCode::Forbidden);
    auto replacement = extra;
    replacement.connectionId = "0";
    REQUIRE(book.issue(replacement).ok());
    auto current = book.consume("0", 1'000);
    REQUIRE(current.ok());
    CHECK(current.value().nonce == replacement.nonce);
    REQUIRE(book.issue(extra).ok());
    book.purgeExpired(1'999);
    CHECK_FALSE(book.issue(replacement).ok());
    book.purgeExpired(2'000);
    REQUIRE(book.issue(replacement).ok());
    CHECK(book.consume("extra", 2'000).ok());
    CHECK_FALSE(book.consume("1", 2'000).ok());
}

TEST_CASE("M2-02 a valid proof for another account cannot answer this challenge") {
    Server server;
    Identity alice, bob;
    alice.save(server.database, "alice");
    bob.save(server.database, "bob");
    TestPeer peer;
    const auto request = server.challenge(peer, "alice");
    CHECK_FALSE(server.respond(peer, bob.response(request, "bob")));
    CHECK_FALSE(server.router.isOnline("bob"));
    CHECK(peer.user.empty());
    // Even a failed attempt burns the challenge.
    CHECK_FALSE(server.respond(peer, alice.response(request, "alice")));
}

TEST_CASE("M2-02 disconnecting the superseded connection preserves its replacement") {
    Server server;
    Identity alice, bob;
    alice.save(server.database, "alice");
    bob.save(server.database, "bob");
    TestPeer oldPeer, replacement, observer;
    server.login(oldPeer, "alice", alice);
    server.login(observer, "bob", bob);
    const auto outstanding = server.challenge(oldPeer, "alice");
    server.login(replacement, "alice", alice);
    CHECK(oldPeer.user.empty());
    REQUIRE_FALSE(oldPeer.inbox.empty());
    CHECK(oldPeer.inbox.back().type == MsgType::Error);
    CHECK(bodyStr(oldPeer.inbox.back().body, "code") == "kicked");
    CHECK_FALSE(server.respond(oldPeer, alice.response(outstanding, "alice")));
    observer.inbox.clear();
    oldPeer.inbox.clear();
    server.router.onMessage(oldPeer, makeSendMsg("old-message", "bob", "chat", "YQ=="));
    REQUIRE(oldPeer.inbox.size() == 1);
    CHECK(bodyStr(oldPeer.inbox.front().body, "code") == "not_logged_in");
    CHECK(observer.inbox.empty());
    server.router.onDisconnect(oldPeer);
    server.router.onDisconnect(oldPeer);
    CHECK(server.router.isOnline("alice"));
    CHECK(server.router.onlineUsers() == std::vector<std::string>{"alice", "bob"});
    CHECK(replacement.user == "alice");
    CHECK(observer.inbox.empty()); // no false offline presence
    replacement.inbox.clear();
    server.router.onMessage(observer, makeSendMsg("new-message", "alice", "chat", "YQ=="));
    REQUIRE(replacement.inbox.size() == 1);
    CHECK(replacement.inbox.front().type == MsgType::RecvMsg);
    CHECK(replacement.inbox.front().msgId == "new-message");
    observer.inbox.clear();
    server.router.onDisconnect(replacement);
    server.router.onDisconnect(replacement);
    CHECK_FALSE(server.router.isOnline("alice"));
    REQUIRE(observer.inbox.size() == 1);
    CHECK(observer.inbox.front().type == MsgType::Presence);
    CHECK_FALSE(bodyBool(observer.inbox.front().body, "online"));
}

TEST_CASE("M2-02 a cached username alone grants neither routing nor logout authority") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer active, stale;
    server.login(active, "alice", alice);
    stale.user = "alice";
    server.router.onMessage(stale, makeListUsers());
    REQUIRE(stale.inbox.size() == 1);
    CHECK(bodyStr(stale.inbox.front().body, "code") == "not_logged_in");
    server.router.onDisconnect(stale);
    CHECK(server.router.isOnline("alice"));
    CHECK(server.router.onlineUsers() == std::vector<std::string>{"alice"});
}

TEST_CASE("M2-02 router enforces the exact challenge expiration boundary") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer peer;
    const auto request = server.challenge(peer, "alice");
    const auto response = alice.response(request, "alice");
    bool shouldSucceed = false;
    SUBCASE("one millisecond before expiry") {
        server.nowMs += ccfw::server::kLoginChallengeLifetimeMs - 1;
        shouldSucceed = true;
    }
    SUBCASE("at expiry") { server.nowMs += ccfw::server::kLoginChallengeLifetimeMs; }
    SUBCASE("after expiry") { server.nowMs += ccfw::server::kLoginChallengeLifetimeMs + 1; }
    CHECK(server.respond(peer, response) == shouldSucceed);
    CHECK(server.router.isOnline("alice") == shouldSucceed);
    CHECK_FALSE(server.respond(peer, response));
    // A consumed or expired challenge does not prevent a fresh login.
    server.login(peer, "alice", alice);
}

TEST_CASE("M2-02 a response on another connection cannot consume the owner's challenge") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer owner, other;
    const auto ownerRequest = server.challenge(owner, "alice");
    const auto ownerResponse = alice.response(ownerRequest, "alice");
    SUBCASE("other connection has no challenge") {
        CHECK_FALSE(server.respond(other, ownerResponse));
    }
    SUBCASE("other connection has a separate challenge for the same account") {
        const auto otherRequest = server.challenge(other, "alice");
        CHECK(bodyStr(ownerRequest.body, "nonce") != bodyStr(otherRequest.body, "nonce"));
        CHECK_FALSE(server.respond(other, ownerResponse));
        CHECK_FALSE(server.respond(other, alice.response(otherRequest, "alice")));
    }
    CHECK(other.user.empty());
    REQUIRE(server.respond(owner, ownerResponse));
    CHECK_FALSE(server.respond(owner, ownerResponse));
}

TEST_CASE("M2-02 every invalid authentication response burns its challenge") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer peer;
    const auto request = server.challenge(peer, "alice");
    const auto valid = alice.response(request, "alice");
    auto invalid = valid;
    SUBCASE("wrong password proof") { invalid.body["proof"] = base64Encode(Bytes(32, '\0')); }
    SUBCASE("bad signature") { invalid.body["sig"] = base64Encode(Bytes(64, '\0')); }
    SUBCASE("wrong nonce") { invalid.body["nonce"] = base64Encode(Bytes(16, '\0')); }
    SUBCASE("malformed proof") { invalid.body["proof"] = "!invalid"; }
    SUBCASE("malformed signature") { invalid.body["sig"] = "!invalid"; }
    SUBCASE("malformed nonce") { invalid.body["nonce"] = "!invalid"; }
    SUBCASE("non-string account") { invalid.body["username"] = 42; }
    SUBCASE("missing proof") { invalid.body.erase("proof"); }
    CHECK_FALSE(server.respond(peer, invalid));
    CHECK_FALSE(server.respond(peer, valid));
    CHECK_FALSE(server.router.isOnline("alice"));
    CHECK(peer.user.empty());
    server.login(peer, "alice", alice);
}

TEST_CASE("M2-02 requesting another challenge invalidates every older response") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer peer;
    const auto oldRequest = server.challenge(peer, "alice");
    const auto newRequest = server.challenge(peer, "alice");
    CHECK(bodyStr(oldRequest.body, "nonce") != bodyStr(newRequest.body, "nonce"));
    SUBCASE("latest response succeeds") {
        REQUIRE(server.respond(peer, alice.response(newRequest, "alice")));
        CHECK_FALSE(server.respond(peer, alice.response(oldRequest, "alice")));
    }
    SUBCASE("older response fails and consumes the pending replacement") {
        CHECK_FALSE(server.respond(peer, alice.response(oldRequest, "alice")));
        CHECK_FALSE(server.respond(peer, alice.response(newRequest, "alice")));
        server.login(peer, "alice", alice);
    }
}

TEST_CASE("M2-02 an invalid new login request also invalidates the previous challenge") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer peer;
    const auto oldRequest = server.challenge(peer, "alice");
    peer.inbox.clear();
    server.router.onMessage(peer, makeLoginRequest("missing-user"));
    REQUIRE(peer.inbox.size() == 1);
    CHECK(peer.inbox.front().type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(peer.inbox.front().body, "ok"));
    CHECK_FALSE(server.respond(peer, alice.response(oldRequest, "alice")));
    server.login(peer, "alice", alice);
}

TEST_CASE("M2-02 disconnecting during authentication invalidates the nonce before peer reuse") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    TestPeer peer;
    const auto oldRequest = server.challenge(peer, "alice");
    server.router.onDisconnect(peer);
    server.router.onDisconnect(peer);
    CHECK_FALSE(server.respond(peer, alice.response(oldRequest, "alice")));
    server.login(peer, "alice", alice);
}

TEST_CASE("M2-02 a logged-in connection cannot retain two account mappings") {
    Server server;
    Identity alice, bob;
    alice.save(server.database, "alice");
    bob.save(server.database, "bob");
    TestPeer peer;
    server.login(peer, "alice", alice);
    const auto pending = server.challenge(peer, "alice");
    peer.inbox.clear();
    server.router.onMessage(peer, makeLoginRequest("bob"));
    REQUIRE(peer.inbox.size() == 1);
    CHECK(peer.inbox.front().type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(peer.inbox.front().body, "ok"));
    CHECK_FALSE(server.respond(peer, alice.response(pending, "alice")));
    CHECK(peer.user == "alice");
    CHECK(server.router.onlineUsers() == std::vector<std::string>{"alice"});
}

TEST_CASE("M2-02 router bounds pending connections and recovers capacity without sleeps") {
    Server server;
    Identity alice;
    alice.save(server.database, "alice");
    // Fixed-size vector keeps every Peer address stable for its connection lifetime.
    std::vector<TestPeer> peers(ccfw::server::kMaxPendingAuth + 1);
    for (std::size_t i = 0; i < ccfw::server::kMaxPendingAuth; ++i) {
        server.challenge(peers[i], "alice");
    }
    server.router.onMessage(peers.back(), makeLoginRequest("alice"));
    REQUIRE(peers.back().inbox.size() == 1);
    CHECK(peers.back().inbox.front().type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(peers.back().inbox.front().body, "ok"));
    CHECK(peers.back().user.empty());
    server.challenge(peers[0], "alice"); // replacement still allowed at capacity
    server.router.onDisconnect(peers[0]);
    server.challenge(peers.back(), "alice"); // disconnect releases a slot
    const auto oldRequest = peers[1].inbox.front();
    server.nowMs += ccfw::server::kLoginChallengeLifetimeMs;
    server.challenge(peers[0], "alice"); // expired entries are reclaimed on issue
    CHECK_FALSE(server.respond(peers[1], alice.response(oldRequest, "alice")));
    server.login(peers[1], "alice", alice);
}
