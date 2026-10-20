#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ccfw/local_store.hpp"
#include "../../src/snapshot_codec.hpp"
#include "../../src/sqlite_helpers.hpp"
#include <chrono>
#include <fstream>

using namespace ccfw;
namespace {
std::filesystem::path freshPath() {
    auto dir = std::filesystem::current_path() / ("M3-03-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir / "client.db";
}
ClientSnapshot filled(ClientSnapshot s) {
    IdentityState identity;
    identity.signing = cc::crypto::generateEd25519();
    identity.identityDh = cc::crypto::generateX25519();
    identity.signedPrekey = cc::crypto::generateX25519();
    identity.signedPrekeySignature = cc::crypto::ed25519Sign(identity.signing.priv, identity.signedPrekey.pub);
    identity.oneTimePrivateKeys[1] = cc::crypto::generateX25519().priv;
    identity.nextPrekeyId = 2;
    s.identity = identity;
    auto peer = cc::crypto::generateX25519();
    s.sessions.push_back({"bob", "session", cc::Bytes(64, 'a'),
        cc::ratchet::DoubleRatchet::initAsInitiator(cc::crypto::randomBytes(32), peer.pub).toJson()});
    s.trustedPeers.push_back({"bob", cc::crypto::generateEd25519().pub, peer.pub, true});
    s.history.push_back({"id", "", "alice", "bob", "private-history-marker", 0, 123, DeliveryState::Queued});
    s.outbox.push_back({"id", "alice", "bob", "chat", "YQ==", 0, 123});
    s.receivedMessages.push_back({"received", "bob", "alice", "chat", 0, "digest"});
    s.pendingPayloads.push_back({"pending", "", "bob", "alice", "file_meta", cc::Bytes("\0payload\xff", 9), 0, 321});
    s.transfers.push_back({"file", "bob", std::filesystem::path("data.part"), 5, 0, cc::crypto::randomBytes(32), {}, "digest"});
    ++s.revision;
    return s;
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
struct ToggleProtector : ISecretProtector {
    PlatformSecretProtector delegate;
    bool fail = false;
    Result<cc::Bytes> seal(cc::BytesView bytes) override {
        if (fail) return Result<cc::Bytes>::failure({ErrorCode::StorageFailure, "M3-03", "injected failure"});
        return delegate.seal(bytes);
    }
    Result<cc::Bytes> unseal(cc::BytesView bytes) override { return delegate.unseal(bytes); }
};
}
TEST_CASE("M3-03 complete protected snapshot survives reopening with identical secrets and state") {
    const auto path = freshPath();
    PlatformSecretProtector protector;
    ClientSnapshot saved;
    {
        SqliteLocalStore store(protector);
        REQUIRE(store.open(path, {"server", "alice"}));
        auto initial = store.load(); REQUIRE(initial);
        CHECK(initial.value().revision == 0);
        CHECK_FALSE(initial.value().identity);
        saved = filled(initial.value());
        REQUIRE(store.commit(0, saved));
        store.close(); store.close();
    }
    {
        SqliteLocalStore reopened(protector);
        REQUIRE(reopened.open(path, {"server", "alice"}));
        auto loaded = reopened.load(); REQUIRE(loaded);
        const bool equal = nlohmann::json(saved) == nlohmann::json(loaded.value());
        CHECK(equal);
    }
    const auto disk = read(path);
    CHECK(disk.find("private-history-marker") == std::string::npos);
    const bool privateAbsent = disk.find(saved.identity->signing.priv) == std::string::npos;
    const bool ratchetAbsent = disk.find(saved.sessions.front().ratchetState.at("rk").get<std::string>()) == std::string::npos;
    CHECK(privateAbsent); CHECK(ratchetAbsent);
}
TEST_CASE("M3-03 concurrent writers use revision CAS and scope mismatch never overwrites data") {
    const auto path = freshPath();
    PlatformSecretProtector protector;
    SqliteLocalStore first(protector), second(protector);
    REQUIRE(first.open(path, {"server", "alice"}));
    REQUIRE(second.open(path, {"server", "alice"}));
    auto next = filled(first.load().value());
    REQUIRE(first.commit(0, next));
    auto stale = second.commit(0, next);
    REQUIRE_FALSE(stale); CHECK(stale.error().code == ErrorCode::Conflict);
    REQUIRE_FALSE(second.open(path, {"another-server", "alice"}));
    REQUIRE_FALSE(second.open(path, {"server", "bob"}));
    auto loaded = second.load(); REQUIRE(loaded);
    CHECK(loaded.value().revision == 1);
    const bool equal = nlohmann::json(loaded.value()) == nlohmann::json(next);
    CHECK(equal);
    next.account.username = "bob"; next.revision = 2;
    CHECK_FALSE(first.commit(1, next));
    CHECK(first.load().value().revision == 1);
}
TEST_CASE("M3-03 failed encryption and transaction leave ratchet history and outbox atomic") {
    const auto path = freshPath();
    ToggleProtector protector;
    SqliteLocalStore store(protector);
    REQUIRE(store.open(path, {"server", "alice"}));
    const auto initial = store.load().value();
    const auto next = filled(initial);
    SUBCASE("protector failure") {
        protector.fail = true;
        CHECK_FALSE(store.commit(0, next));
    }
    SUBCASE("database transaction abort") {
        sql::Database database; database.open(path.string());
        sql::exec(database.value, "CREATE TRIGGER fail_update AFTER UPDATE ON client_state BEGIN SELECT RAISE(ABORT,'injected'); END");
        CHECK_FALSE(store.commit(0, next));
        sql::exec(database.value, "DROP TRIGGER fail_update");
    }
    const auto after = store.load(); REQUIRE(after);
    const bool unchanged = nlohmann::json(after.value()) == nlohmann::json(initial);
    CHECK(unchanged);
    protector.fail = false;
    REQUIRE(store.commit(0, next));
    CHECK(store.load().value().revision == 1);
}
TEST_CASE("M3-03 damaged files and incompatible snapshot metadata are rejected") {
    const auto path = freshPath();
    PlatformSecretProtector protector;
    {
        SqliteLocalStore store(protector); REQUIRE(store.open(path, {"server", "alice"}));
        REQUIRE(store.commit(0, filled(store.load().value())));
    }
    SUBCASE("altered protected bytes") {
        sql::Database db; db.open(path.string());
        sql::exec(db.value, "UPDATE client_state SET protected_snapshot=x'00112233'");
    }
    SUBCASE("revision differs from authenticated snapshot") {
        sql::Database db; db.open(path.string()); sql::exec(db.value, "UPDATE client_state SET revision=7");
    }
    SUBCASE("unsupported schema") {
        sql::Database db; db.open(path.string()); sql::exec(db.value, "PRAGMA user_version=9");
    }
    SUBCASE("missing state row") {
        sql::Database db; db.open(path.string()); sql::exec(db.value, "DELETE FROM client_state");
    }
    SUBCASE("not a database") { std::ofstream(path, std::ios::binary | std::ios::trunc) << "broken"; }
    SqliteLocalStore store(protector);
    CHECK_FALSE(store.open(path, {"server", "alice"}));
    CHECK_FALSE(store.load());
}
TEST_CASE("M3-03 cached state still detects external same-revision ciphertext corruption") {
    const auto path = freshPath(); PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(path, {"server", "alice"})); REQUIRE(store.load());
    sql::Database external; external.open(path.string());
    sql::exec(external.value, "UPDATE client_state SET protected_snapshot=x'00112233'");
    CHECK_FALSE(store.load());
}
