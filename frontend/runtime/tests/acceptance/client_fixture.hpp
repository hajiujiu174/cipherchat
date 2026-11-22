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
    // 发布测试。
    void publish(ClientEvent event) override { items.push_back(std::move(event)); }
    // 返回测试使用的of类型。
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
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit RealServer(std::filesystem::path path = {}, bool fileCache = true) : directory(path.empty() ? tempDirectory("real-server") : std::move(path)) {
        router.setFileCacheEnabled(fileCache);
        worker = std::async(std::launch::async, [this] { return tls.listen(0, "127.0.0.1"); });
        REQUIRE(tls.waitReady(3000));
    }
    // 销毁测试时释放它持有的资源。
    ~RealServer() { tls.stop(); if (worker.valid()) worker.wait(); }
    // 返回测试使用的选项。
    ConnectionOptions options() const { return {"127.0.0.1", tls.port(), directory / "cert.pem", "localhost", 3000}; }
};
struct ToggleStore final : ILocalStore {
    ILocalStore& inner;
    bool fail = false;
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit ToggleStore(ILocalStore& inner) : inner(inner) {}
    // 打开测试。
    Result<> open(const std::filesystem::path& p, const AccountScope& a) override { return inner.open(p, a); }
    // 加载测试。
    Result<ClientSnapshot> load() override { return inner.load(); }
    // 提交测试。
    Result<> commit(std::uint64_t revision, const ClientSnapshot& next) override {
        if (fail) return Result<>::failure({ErrorCode::StorageFailure, "test", "injected failure"});
        return inner.commit(revision, next);
    }
    // 关闭测试。
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
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit RealClient(std::string name, std::filesystem::path directory = {})
        : directory(directory.empty() ? tempDirectory(name) : std::move(directory)), name(std::move(name)), groups(chat, net, {"real-test-server", this->name}, &store) {
        REQUIRE(store.open(this->directory / "client.db", {"real-test-server", this->name}));
        REQUIRE(files.setDownloadDirectory(this->directory / "downloads"));
    }
    // 销毁测试时释放它持有的资源。
    ~RealClient() { application.shutdown(); }
    // 验证“connect”场景中的关键结果，断言失败时由测试框架报告。
    void connect(const ConnectionOptions& options) {
        REQUIRE(application.connect(options));
        REQUIRE(eventually([&] { application.tick(); return account.phase() == AccountPhase::Connected; }));
    }
    // 验证“registerAnd登录”场景中的关键结果，断言失败时由测试框架报告。
    void registerAndLogin(const ConnectionOptions& options) {
        connect(options);
        REQUIRE(account.registerAccount(name, "password123"));
        REQUIRE(eventually([&] { application.tick(); return account.phase() == AccountPhase::Connected; }));
        login();
    }
    // 验证“登录”场景中的关键结果，断言失败时由测试框架报告。
    void login() {
        REQUIRE(account.login(name, "password123"));
        REQUIRE(eventually([&] { application.tick(); return account.phase() == AccountPhase::Online; }));
    }
    // 验证“settle”场景中的关键结果，断言失败时由测试框架报告。
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
