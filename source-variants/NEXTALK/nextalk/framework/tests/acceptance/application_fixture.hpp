#pragma once
#include "client_fixture.hpp"
#include "account_fixture.hpp"

namespace ccfw::test {
struct ThreadStore final : ILocalStore {
    ILocalStore& inner;
    std::vector<std::thread::id> calls;
    explicit ThreadStore(ILocalStore& inner) : inner(inner) {}
    Result<> open(const std::filesystem::path& path, const AccountScope& scope) override { calls.push_back(std::this_thread::get_id()); return inner.open(path, scope); }
    Result<ClientSnapshot> load() override { calls.push_back(std::this_thread::get_id()); return inner.load(); }
    Result<> commit(std::uint64_t revision, const ClientSnapshot& snapshot) override { calls.push_back(std::this_thread::get_id()); return inner.commit(revision, snapshot); }
    void close() noexcept override { inner.close(); }
};
struct SessionSpy final : ISessionService {
    SessionService inner;
    std::vector<std::string> decryptedKinds;
    explicit SessionSpy(ILocalStore& store) : inner(store) {}
    Result<> establish(const std::string& peer, const cc::ratchet::X3dhPeerBundle& bundle) override { return inner.establish(peer, bundle); }
    Result<WireEnvelope> encryptAndCommit(const PlainEnvelope& p) override { return inner.encryptAndCommit(p); }
    Result<PlainEnvelope> decryptAndCommit(const WireEnvelope& p) override { decryptedKinds.push_back(p.kind); return inner.decryptAndCommit(p); }
};
struct ThreadEvents final : IEventSink {
    Events events;
    std::vector<std::thread::id> callers;
    void publish(ClientEvent event) override { callers.push_back(std::this_thread::get_id()); events.publish(std::move(event)); }
};
struct AppFixture {
    cc::server::Storage database{""}; cc::server::Router router{database}; RouterTransport net{router};
    PlatformSecretProtector protector; SqliteLocalStore disk{protector}; ThreadStore store{disk}; SessionSpy sessions{store};
    AccountService account{net, store}; ChatService chat{net, sessions, store}; FileTransferService files{net, store, sessions};
    std::int64_t now = 1000;
    GroupService groups{chat, net, {"app-test", "alice"}, &store, [this] { return now; }};
    ThreadEvents sink; ClientApplication app{net, account, chat, files, groups, sink, [this] { return now; }};
    AppFixture() {
        const auto directory = tempDirectory("application"); REQUIRE(store.open(directory / "client.db", {"app-test", "alice"}));
        REQUIRE(files.setDownloadDirectory(directory / "downloads"));
        REQUIRE(app.connect({})); REQUIRE(app.tick()); REQUIRE(account.phase() == AccountPhase::Connected);
        REQUIRE(account.registerAccount("alice", "password123")); REQUIRE(app.tick());
        REQUIRE(account.login("alice", "password123")); REQUIRE(app.tick()); REQUIRE(account.phase() == AccountPhase::Online);
        net.peer.inbox.clear(); sink.events.items.clear(); sink.callers.clear(); store.calls.clear();
    }
    ~AppFixture() { app.shutdown(); }
    void inject(cc::proto::Message message) { net.peer.inbox.push_back(cc::proto::withV2(std::move(message))); }
};
}
