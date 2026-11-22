#pragma once
#include "server_fixture.hpp"
#include "ccfw/account_service.hpp"

namespace ccfw::test {
class RouterTransport final : public ITransport {
public:
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit RouterTransport(cc::server::Router& router) : router(router) {}
    // 销毁测试时释放它持有的资源。
    ~RouterTransport() override { close(); }
    // 建立连接测试。
    Result<> connect(const ConnectionOptions&) override { connected = true; return Result<>::success({}); }
    // 发送测试。
    Result<> send(const cc::proto::Message& message) override {
        if (!connected || rejectSend) return Result<>::failure({ErrorCode::NotConnected, "test", "disconnected"});
        if (observe) observe(message);
        router.onMessage(peer, message); ++sent;
        return Result<>::success({});
    }
    // 轮询测试。
    Result<std::optional<cc::proto::Message>> poll() override {
        ++polls;
        if (!peer.inbox.empty()) {
            auto message = peer.inbox.front(); peer.inbox.erase(peer.inbox.begin());
            return Result<std::optional<cc::proto::Message>>::success(std::move(message));
        }
        if (!connected) return Result<std::optional<cc::proto::Message>>::failure({ErrorCode::NotConnected, "test", "disconnected"});
        return Result<std::optional<cc::proto::Message>>::success(std::nullopt);
    }
    // 关闭测试。
    void close() noexcept override {
        if (connected) router.onDisconnect(peer);
        connected = false; peer.inbox.clear();
    }
    cc::server::Router& router;
    test::Peer peer;
    std::function<void(const cc::proto::Message&)> observe;
    bool connected = false, rejectSend = false;
    int sent = 0, polls = 0;
};
// 推进账号。
inline Result<> pumpAccount(AccountService& account, RouterTransport& net) {
    for (int i = 0; i < 32; ++i) {
        auto message = net.poll();
        if (!message) return Result<>::failure(message.error());
        if (!message.value()) return Result<>::success({});
        auto handled = account.handle(*message.value());
        if (!handled) return handled;
    }
    return Result<>::failure({ErrorCode::ProtocolError, "test", "unexpected response loop"});
}
// 验证“negotiate”场景中的关键结果，断言失败时由测试框架报告。
inline void negotiate(AccountService& account, RouterTransport& net) {
    REQUIRE(net.connect({})); account.connectionChanged(true);
    REQUIRE(account.negotiateProtocol()); REQUIRE(pumpAccount(account, net));
    REQUIRE(account.phase() == AccountPhase::Connected);
}
} // namespace ccfw::test
