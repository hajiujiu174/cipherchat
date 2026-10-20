// 端到端集成测试：真实 TLS 服务器 + 两个 ClientCore 实例。
// 验证注册 → 登录 → prekey → X3DH 建会话 → 双向密文收发 → 离线补投全链路。
#include <doctest/doctest.h>

#include "client/core/client.hpp"
#include "server/net/tls_server.hpp"
#include "server/router/router.hpp"
#include "server/storage/storage.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <tuple>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace cc;

// 事件收集器：把回调事件排队，测试线程带超时地等待
template <typename T>
class EventQueue {
public:
    void push(T v) {
        std::lock_guard<std::mutex> lock(m_);
        q_.push_back(std::move(v));
        cv_.notify_all();
    }
    template <typename Pred>
    bool waitFor(Pred pred, int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
            return pred(q_);
        });
    }
    std::vector<T> snapshot() {
        std::lock_guard<std::mutex> lock(m_);
        return q_;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<T> q_;
};

struct TestServer {
    server::Storage db{""};
    server::Router router{db};
    server::TlsServer tls{router, "", ""};
    mutable std::mutex logMutex;
    std::vector<std::string> logs;

    void start(std::uint16_t port) {
        tls.setLogger([this](const std::string& line) {
            std::lock_guard<std::mutex> lock(logMutex);
            logs.push_back(line);
        });
        started_ = std::async(std::launch::async,
                              [this, port] { return tls.listen(port); });
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    void checkStarted() {
        // 若监听循环已退出（bind 失败等），结果必须为 true
        if (started_.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
            REQUIRE(started_.get());
        }
    }
    void dumpLogs() const {
        std::lock_guard<std::mutex> lock(logMutex);
        for (const auto& l : logs) {
            std::fprintf(stderr, "  [srv] %s\n", l.c_str());
        }
    }
    ~TestServer() {
        tls.stop();
        if (started_.valid()) started_.wait();
    }
    std::future<bool> started_;
};

using RecvEvent = std::tuple<std::string, std::string>;  // from, text

struct ClientHarness {
    client::ClientCore client;
    EventQueue<bool> registerDone;
    EventQueue<bool> loginDone;
    EventQueue<RecvEvent> messages;
    EventQueue<std::string> errors;

    void wire() {
        client.onRegisterResult = [this](bool ok, const std::string& err) {
            if (!ok) errors.push("register: " + err);
            registerDone.push(ok);
        };
        client.onLoginResult = [this](bool ok, const std::string& err) {
            if (!ok) errors.push("login: " + err);
            loginDone.push(ok);
        };
        client.onMessage = [this](const std::string& from,
                                  const std::string& text, std::int64_t) {
            messages.push({from, text});
        };
        client.onError = [this](const std::string& code,
                                const std::string& text) {
            errors.push(code + ": " + text);
        };
    }

    bool waitRegister(int ms = 8000, TestServer* srv = nullptr) {
        const bool ok = registerDone.waitFor(
            [](const std::vector<bool>& q) { return !q.empty(); }, ms);
        if (!ok && srv) srv->dumpLogs();
        return ok;
    }
    bool waitLogin(int ms = 8000) {
        return loginDone.waitFor(
            [](const std::vector<bool>& q) { return !q.empty(); }, ms);
    }
    bool waitMessageCount(std::size_t n, int ms = 8000) {
        return messages.waitFor(
            [n](const std::vector<RecvEvent>& q) { return q.size() >= n; }, ms);
    }
};

constexpr std::uint16_t kPort = 55241;

}  // namespace

TEST_CASE("E2E-1 双客户端注册登录并互发加密消息") {
    TestServer server;
    server.start(kPort);
    server.checkStarted();

    ClientHarness alice, bob;
    alice.wire();
    bob.wire();

    // ---- 注册 ----
    alice.client.connect("127.0.0.1", kPort);
    alice.client.registerAccount("alice", "口令-alice-1");
    REQUIRE(alice.waitRegister(8000, &server));
    CHECK(alice.errors.snapshot().empty());

    bob.client.connect("127.0.0.1", kPort);
    bob.client.registerAccount("bob", "口令-bob-1");
    REQUIRE(bob.waitRegister(8000, &server));

    // ---- 登录（挑战-响应）+ 上传一次性密钥 ----
    alice.client.login("alice", "口令-alice-1");
    REQUIRE(alice.waitLogin());
    alice.client.uploadPrekeys(8);

    bob.client.login("bob", "口令-bob-1");
    REQUIRE(bob.waitLogin());
    bob.client.uploadPrekeys(8);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ---- alice 首次发消息：fetch_bundle → X3DH → 双棘轮 ----
    alice.client.sendChat("bob", "你好 Bob，这是端到端加密的第 1 条");
    REQUIRE(bob.waitMessageCount(1));
    CHECK(std::get<1>(bob.messages.snapshot()[0]) ==
          "你好 Bob，这是端到端加密的第 1 条");
    CHECK(std::get<0>(bob.messages.snapshot()[0]) == "alice");
    CHECK(bob.errors.snapshot().empty());

    // ---- bob 回复：直接复用会话（方向切换 → DH 棘轮）----
    bob.client.sendChat("alice", "收到！回复走的是 DH 棘轮后的新链");
    REQUIRE(alice.waitMessageCount(1));
    CHECK(std::get<1>(alice.messages.snapshot()[0]) ==
          "收到！回复走的是 DH 棘轮后的新链");

    // ---- 多轮往返 + 长消息 ----
    const std::string longText = [] {
        // 2500 个汉字 = 7500 字节 UTF-8，覆盖多块与多字节边界
        std::string s;
        for (int i = 0; i < 2500; ++i) s += "长";
        return s;
    }();
    for (int i = 0; i < 6; ++i) {
        alice.client.sendChat("bob", "A->B 第 " + std::to_string(i) + " 轮");
        bob.client.sendChat("alice", "B->A 第 " + std::to_string(i) + " 轮");
    }
    alice.client.sendChat("bob", longText);
    REQUIRE(bob.waitMessageCount(8));  // 1 + 6 + 1
    REQUIRE(alice.waitMessageCount(7));
    auto bobMsgs = bob.messages.snapshot();
    CHECK(std::get<1>(bobMsgs[7]) == longText);  // 全部按序解密
    CHECK(std::get<1>(bobMsgs[1]) == "A->B 第 0 轮");
    CHECK(std::get<1>(bobMsgs[6]) == "A->B 第 5 轮");
    auto aliceMsgs = alice.messages.snapshot();
    CHECK(std::get<1>(aliceMsgs[6]) == "B->A 第 5 轮");
    CHECK(alice.errors.snapshot().empty());
    CHECK(bob.errors.snapshot().empty());

    // 双方会话已建立
    CHECK(alice.client.hasSessionWith("bob"));
    CHECK(bob.client.hasSessionWith("alice"));
}

TEST_CASE("E2E-2 离线消息在登录后补投") {
    TestServer server;
    server.start(kPort + 1);
    server.checkStarted();

    ClientHarness alice, bob;
    alice.wire();
    bob.wire();

    alice.client.connect("127.0.0.1", kPort + 1);
    alice.client.registerAccount("alice", "口令-alice-1");
    REQUIRE(alice.waitRegister(8000, &server));
    alice.client.login("alice", "口令-alice-1");
    REQUIRE(alice.waitLogin());

    bob.client.connect("127.0.0.1", kPort + 1);
    bob.client.registerAccount("bob", "口令-bob-1");
    REQUIRE(bob.waitRegister(8000, &server));
    // bob 不登录、不上传密钥 → alice 拿到的束没有一次性密钥（弱化路径）
    alice.client.sendChat("bob", "你还不在线，先存着");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // bob 上线 → 收到补投的 prekey 消息并建立会话解密
    bob.client.login("bob", "口令-bob-1");
    REQUIRE(bob.waitLogin());
    REQUIRE(bob.waitMessageCount(1, 10000));
    auto msgs = bob.messages.snapshot();
    REQUIRE(msgs.size() == 1);
    CHECK(std::get<0>(msgs[0]) == "alice");
    CHECK(std::get<1>(msgs[0]) == "你还不在线，先存着");
    CHECK(bob.errors.snapshot().empty());

    // bob 回复 → 会话正常工作
    bob.client.sendChat("alice", "补投收悉");
    REQUIRE(alice.waitMessageCount(1, 10000));
    CHECK(std::get<1>(alice.messages.snapshot()[0]) == "补投收悉");
}

TEST_CASE("E2E-3 错口令登录被拒绝") {
    TestServer server;
    server.start(kPort + 2);
    server.checkStarted();

    ClientHarness carol;
    carol.wire();
    carol.client.connect("127.0.0.1", kPort + 2);
    carol.client.registerAccount("carol", "correct-horse");
    REQUIRE(carol.waitRegister(8000, &server));

    carol.client.login("carol", "wrong-battery");
    REQUIRE(carol.waitLogin());
    // 登录失败事件 ok=false（wire() 未把失败计入 errors，这里看 loginDone 首值）
    CHECK(carol.loginDone.snapshot()[0] == false);
}

namespace {

// 写一个临时二进制文件，返回路径；内容伪随机可复现
std::string makeTempFile(const std::string& name, std::size_t size) {
    const std::string dir = [] {
        std::error_code ec;
        std::filesystem::create_directories("test-tmp", ec);
        return std::string("test-tmp");
    }();
    const std::string path = dir + "/" + name;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    unsigned char seed = 0x5A;
    for (std::size_t i = 0; i < size; ++i) {
        seed = static_cast<unsigned char>(seed * 31 + i);
        f.put(static_cast<char>(seed));
    }
    return path;
}

bool filesEqual(const std::string& a, const std::string& b) {
    Bytes da, db;
    if (!files::readFile(a, da) || !files::readFile(b, db)) return false;
    return da == db;
}

}  // namespace

TEST_CASE("E2E-4 加密文件传输（多分块 + 逐块确认）") {
    TestServer server;
    server.start(kPort + 3);
    server.checkStarted();

    ClientHarness alice, bob;
    alice.wire();
    bob.wire();

    // bob 接收文件落盘目录
    std::filesystem::create_directories("test-downloads");
    bob.client.setDownloadsDir("test-downloads");

    EventQueue<std::string> bobFileDone;  // 完成时的保存路径
    bob.client.onFileProgress = [&bobFileDone](const std::string&, const std::string&,
                                               std::size_t rec, std::size_t tot, bool done,
                                               const std::string& path) {
        std::fprintf(stderr, "[bob-progress] %zu/%zu done=%d\n", rec, tot,
                     done ? 1 : 0);
        if (done && !path.empty()) bobFileDone.push(path);
    };
    EventQueue<std::string> aliceFileDone;
    alice.client.onFileProgress = [&aliceFileDone](const std::string&, const std::string&,
                                                   std::size_t received, std::size_t total,
                                                   bool done, const std::string&) {
        std::fprintf(stderr, "[alice-progress] %zu/%zu done=%d\n", received,
                     total, done ? 1 : 0);
        if (done) aliceFileDone.push("sent:" + std::to_string(received) + "/" +
                                     std::to_string(total));
    };

    alice.client.connect("127.0.0.1", kPort + 3);
    alice.client.registerAccount("alice", "口令-alice-1");
    REQUIRE(alice.waitRegister(8000, &server));
    alice.client.login("alice", "口令-alice-1");
    REQUIRE(alice.waitLogin());
    alice.client.uploadPrekeys(4);

    bob.client.connect("127.0.0.1", kPort + 3);
    bob.client.registerAccount("bob", "口令-bob-1");
    REQUIRE(bob.waitRegister(8000, &server));
    bob.client.login("bob", "口令-bob-1");
    REQUIRE(bob.waitLogin());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 300KB 源文件 → 5 个分块
    const std::string srcPath = makeTempFile("report.bin", 300 * 1024 + 17);
    REQUIRE(alice.client.sendFile("bob", srcPath));

    // 发送方：全部块确认
    if (!aliceFileDone.waitFor(
            [](const std::vector<std::string>& q) { return !q.empty(); },
            15000)) {
        server.dumpLogs();
        FAIL("发送方未在时限内收到全部块确认");
    }
    // 接收方：落盘完成
    REQUIRE(bobFileDone.waitFor(
        [](const std::vector<std::string>& q) { return !q.empty(); }, 15000));
    const std::string saved = bobFileDone.snapshot().back();
    CHECK(filesEqual(srcPath, saved));
    CHECK(bob.errors.snapshot().empty());
}
