#pragma once
#include "ccfw/application.hpp"
#include "server/net/tls_server.hpp"
#include "server_fixture.hpp"
#include "tls_fixture.hpp"
#include "../../src/snapshot_codec.hpp"
#include <future>

namespace ccfw::test {
struct Events final : IEventSink {
    std::vector<ClientEvent> items;
    void publish(ClientEvent event) override { items.push_back(std::move(event)); }
    template<class T> std::vector<T> ofType() const {
        std::vector<T> results;
        for (const auto& event : items) if (const auto* value = std::get_if<T>(&event)) results.push_back(*value);
        return results;
    }
};
struct RealServer {
    std::filesystem::path directory = tempDirectory("real-server");
    cc::server::Storage database{(directory / "server.db").string()};
    cc::server::Router router{database};
    cc::server::TlsServer tls{router, (directory / "cert.pem").string(), (directory / "key.pem").string()};
    std::future<bool> worker;
    explicit RealServer(std::filesystem::path path = {}, bool fileCache = true) : directory(path.empty() ? tempDirectory("real-server") : std::move(path)) {
        router.setFileCacheEnabled(fileCache);
        worker = std::async(std::launch::async, [this] { return tls.listen(0); });
        REQUIRE(tls.waitReady(3000));
    }
    ~RealServer() { tls.stop(); if (worker.valid()) worker.wait(); }
    ConnectionOptions options() const { return {"127.0.0.1", tls.port(), directory / "cert.pem", "localhost", 3000}; }
};
struct ToggleStore final : ILocalStore {
    ILocalStore& inner;
    bool fail = false;
    explicit ToggleStore(ILocalStore& inner) : inner(inner) {}
    Result<> open(const std::filesystem::path& p, const AccountScope& a) override { return inner.open(p, a); }
    Result<ClientSnapshot> load() override { return inner.load(); }
    Result<> commit(std::uint64_t revision, const ClientSnapshot& next) override {
        if (fail) return Result<>::failure({ErrorCode::StorageFailure, "test", "injected failure"});
        return inner.commit(revision, next);
    }
    void close() noexcept override { inner.close(); }
};
struct RealClient {
    std::filesystem::path directory;
    std::string name;
    PlatformSecretProtector protector;
    SqliteLocalStore disk{protector};
    ToggleStore store{disk};
    TlsTransport net;
    SessionService sessions{store};
    AccountService account{net, store};
    ChatService chat{net, sessions, store};
    FileTransferService files{net, store, sessions};
    GroupService groups;
    Events events;
    ClientApplication application{net, account, chat, files, groups, events};
    explicit RealClient(std::string name, std::filesystem::path directory = {})
        : directory(directory.empty() ? tempDirectory(name) : std::move(directory)), name(std::move(name)), groups(chat, net, {"real-test-server", this->name}, &store) {
        REQUIRE(store.open(this->directory / "client.db", {"real-test-server", this->name}));
        REQUIRE(files.setDownloadDirectory(this->directory / "downloads"));
    }
    ~RealClient() { application.shutdown(); }
    void connect(const ConnectionOptions& options) {
        REQUIRE(application.connect(options));
        REQUIRE(eventually([&] { application.tick(); return account.phase() == AccountPhase::Connected; }));
    }
    void registerAndLogin(const ConnectionOptions& options) {
        connect(options);
        REQUIRE(account.registerAccount(name, "password123"));
        REQUIRE(eventually([&] { application.tick(); return account.phase() == AccountPhase::Connected; }));
        login();
    }
    void login() {
        REQUIRE(account.login(name, "password123"));
        REQUIRE(eventually([&] { application.tick(); return account.phase() == AccountPhase::Online; }));
        REQUIRE(account.uploadPrekeys(4));
    }
    void trust(RealClient& other) {
        const auto snapshot = other.store.load(); REQUIRE(snapshot);
        const auto& identity = *snapshot.value().identity;
        REQUIRE(sessions.acceptPeerIdentity({other.name, identity.signing.pub, identity.identityDh.pub, true}));
    }
    void settle(RealClient& other, const std::string& id) {
        REQUIRE(eventually([&] {
            application.tick(); other.application.tick();
            const auto snapshot = store.load(); if (!snapshot) return false;
            return std::any_of(snapshot.value().history.begin(), snapshot.value().history.end(), [&](const auto& h) {
                return h.messageId == id && h.state == DeliveryState::RecipientReceived;
            });
        }, 5000));
    }
};
} // namespace ccfw::test
