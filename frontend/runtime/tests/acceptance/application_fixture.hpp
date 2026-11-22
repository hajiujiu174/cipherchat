#pragma once
#include "client_fixture.hpp"
#include "account_fixture.hpp"

namespace ccfw::test {
struct ThreadStore final : ILocalStore {
    ILocalStore& inner;
    std::vector<std::thread::id> calls;
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit ThreadStore(ILocalStore& inner) : inner(inner) {}
    // 打开测试。
    Result<> open(const std::filesystem::path& path, const AccountScope& scope) override { calls.push_back(std::this_thread::get_id()); return inner.open(path, scope); }
    // 加载测试。
    Result<ClientSnapshot> load() override { calls.push_back(std::this_thread::get_id()); return inner.load(); }
    // 提交测试。
    Result<> commit(std::uint64_t revision, const ClientSnapshot& snapshot) override { calls.push_back(std::this_thread::get_id()); return inner.commit(revision, snapshot); }
    // 关闭测试。
    void close() noexcept override { inner.close(); }
};
struct SessionSpy final : ISessionService {
    SessionService inner;
    std::vector<std::string> decryptedKinds;
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit SessionSpy(ILocalStore& store) : inner(store) {}
    // 返回测试使用的encryptAnd提交。
    Result<WireEnvelope> encryptAndCommit(const PlainEnvelope& p) override { return inner.encryptAndCommit(p); }
    // 解密And提交。
    Result<PlainEnvelope> decryptAndCommit(const WireEnvelope& p) override { decryptedKinds.push_back(p.kind); return inner.decryptAndCommit(p); }
};
struct ThreadEvents final : IEventSink {
    Events events;
    std::vector<std::thread::id> callers;
    // 发布测试。
    void publish(ClientEvent event) override { callers.push_back(std::this_thread::get_id()); events.publish(std::move(event)); }
};
struct AppFixture {
    cc::server::Storage database{""}; cc::server::Router router{database}; RouterTransport net{router};
    PlatformSecretProtector protector; SqliteLocalStore disk{protector}; ThreadStore store{disk}; SessionSpy sessions{store};
    AccountService account{net, store}; ChatService chat{net, sessions, store}; FileTransferService files{net, store, sessions};
    std::int64_t now = 1000;
    GroupService groups{chat, net, {"app-test", "alice"}, &store, [this] { return now; }};
    ThreadEvents sink; ClientApplication app{net, account, chat, files, groups, sink, [this] { return now; }};
    // 构造测试，初始化后续操作需要的状态或依赖。
    AppFixture() {
        const auto directory = tempDirectory("application"); REQUIRE(store.open(directory / "client.db", {"app-test", "alice"}));
        REQUIRE(files.setDownloadDirectory(directory / "downloads"));
        REQUIRE(app.connect({})); REQUIRE(app.tick()); REQUIRE(account.phase() == AccountPhase::Connected);
        REQUIRE(account.registerAccount("alice", "password123")); REQUIRE(app.tick());
        REQUIRE(account.login("alice", "password123")); REQUIRE(app.tick()); REQUIRE(account.phase() == AccountPhase::Online);
        net.peer.inbox.clear(); sink.events.items.clear(); sink.callers.clear(); store.calls.clear();
    }
    // 销毁测试时释放它持有的资源。
    ~AppFixture() { app.shutdown(); }
    // 更新测试中的inject状态。
    void inject(cc::proto::Message message) { net.peer.inbox.push_back(cc::proto::withV2(std::move(message))); }
};
}
