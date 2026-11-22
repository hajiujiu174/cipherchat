// M0-02 验收：传输层并发修复的回归测试。
// 根因回顾：原实现中主线程 SSL_write 与接收线程 SSL_read 并发访问同一
// SSL 对象，OpenSSL 3.6 下会静默丢失记录（客户端写成功、服务器收不到），
// 表现为 E2E 集成测试注册等待超时。
// 修复：SSL 对象全程单线程拥有（握手移入收发线程），发送改为有界出站
// 队列由收发线程统一写出，poll 空闲窗口驱动；TLS 1.3 后握手指令
// （NewSessionTicket → SSL_ERROR_WANT_READ）不再误判为连接结束。
// 本测试用真实 TLS 服务器 + 真实 NetClient 压力复现原竞态路径：
// 连续注册往返与并发双客户端注册，全部必须按序得到服务器应答。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "client/core/net_client.hpp"
#include "common/protocol/messages.hpp"
#include "server/net/tls_server.hpp"
#include "server/router/router.hpp"
#include "server/storage/storage.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace cc;

// 单个连接的事件等待器
struct ReplyBox {
    std::mutex m;
    std::condition_variable cv;
    std::vector<cc::proto::Message> replies;

    void push(const cc::proto::Message& msg) {
        {
            std::lock_guard<std::mutex> lock(m);
            replies.push_back(msg);
        }
        cv.notify_all();
    }
    template <typename Pred>
    bool wait(Pred pred, int timeoutMs) {
        std::unique_lock<std::mutex> lock(m);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [&] { return pred(replies); });
    }
};

struct TestServer {
    server::Storage db{""};
    server::Router router{db};
    server::TlsServer tls{router, "", ""};
    std::future<bool> worker;

    void start(std::uint16_t port) {
        worker = std::async(std::launch::async,
                            [this, port] { return tls.listen(port); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    ~TestServer() {
        tls.stop();
        if (worker.valid()) worker.wait();
    }
};

// 发起一次注册并等待 register_result；返回是否在时限内收到应答
bool registerRoundTrip(std::uint16_t port, const std::string& username) {
    client::NetClient net;
    ReplyBox box;
    bool connected = false;
    try {
        net.start(
            "127.0.0.1", port,
            [&box](const cc::proto::Message& msg) { box.push(msg); },
            [&connected] { connected = false; });
    } catch (const std::exception&) {
        return false;
    }
    connected = true;
    const bool sent =
        net.send(cc::proto::makeRegister(username, "pw-" + username, "QUJD",
                                         "REVG", "R0hJ", "SktM"));
    if (!sent) return false;
    const bool got = box.wait(
        [&](const std::vector<cc::proto::Message>& r) {
            for (const auto& m : r) {
                if (m.type == cc::proto::MsgType::RegisterResult) return true;
            }
            return false;
        },
        5000);
    net.stop();
    return got;
}

constexpr std::uint16_t kPort = 55250;

}  // namespace

TEST_CASE("M0-02 连续 12 次注册往返全部按时应答（原竞态回归）") {
    TestServer server;
    server.start(kPort);
    for (int i = 0; i < 12; ++i) {
        INFO("iteration ", i);
        CHECK(registerRoundTrip(kPort, "user" + std::to_string(i)));
    }
}

TEST_CASE("M0-02 并发双客户端同时注册均得到应答") {
    TestServer server;
    server.start(kPort + 1);

    client::NetClient a, b;
    ReplyBox boxA, boxB;
    REQUIRE_NOTHROW(a.start(
        "127.0.0.1", kPort + 1,
        [&boxA](const cc::proto::Message& m) { boxA.push(m); }, [] {}));
    REQUIRE_NOTHROW(b.start(
        "127.0.0.1", kPort + 1,
        [&boxB](const cc::proto::Message& m) { boxB.push(m); }, [] {}));

    // 同时发出（两连接各自出站队列，收发线程统一写出）
    REQUIRE(a.send(cc::proto::makeRegister("alice", "pw-alice-1", "QUJD",
                                           "REVG", "R0hJ", "SktM")));
    REQUIRE(b.send(cc::proto::makeRegister("bob", "pw-bob-1", "QUJD", "REVG",
                                           "R0hJ", "SktM")));

    const auto hasResult = [](std::vector<cc::proto::Message>& r) {
        for (const auto& m : r) {
            if (m.type == cc::proto::MsgType::RegisterResult) return true;
        }
        return false;
    };
    CHECK(boxA.wait(hasResult, 5000));
    CHECK(boxB.wait(hasResult, 5000));
    a.stop();
    b.stop();
}
