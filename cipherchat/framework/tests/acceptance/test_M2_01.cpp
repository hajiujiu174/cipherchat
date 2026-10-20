// M2-01 验收：TLS 服务身份验证。
// 用内存 CA + 最小 TLS 监听替身（仅测试内），验证正式路径 startSecure：
//   - 合法 CA + 正确主机名 → 握手成功；
//   - 未知 CA / 错误主机名 / 过期证书 → 握手失败；
//   - 失败连接不发出任何业务帧（口令/认证证明不上线）。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "client/core/net_client.hpp"
#include "common/net/socket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

using namespace cc;

constexpr std::uint16_t kBasePort = 55310;

// 最小 TLS 监听替身：逐个 accept + SSL_accept，握手后立即关闭。
class FakeTlsListener {
public:
    FakeTlsListener(net::SslCtx ctx, std::uint16_t port)
        : ctx_(std::move(ctx)) {
#ifdef _WIN32
        static bool wsa = [] {
            WSADATA d;
            WSAStartup(MAKEWORD(2, 2), &d);
            return true;
        }();
        (void)wsa;
#endif
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(listenFd_ != net::invalidSocket);
        int reuse = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        REQUIRE(::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr),
                       sizeof(addr)) == 0);
        REQUIRE(::listen(listenFd_, 4) == 0);
        worker_ = std::thread([this] { loop(); });
    }
    ~FakeTlsListener() {
        stop_ = true;
#ifdef _WIN32
        ::closesocket(listenFd_);
#else
        ::close(listenFd_);
#endif
        if (worker_.joinable()) worker_.join();
    }

private:
    void loop() {
        while (!stop_.load()) {
            // select 短超时轮询：Windows 下跨线程 closesocket 不保证唤醒
            // 阻塞中的 accept()，不能用无限期阻塞式 accept。
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listenFd_, &readSet);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            const int ready = ::select(static_cast<int>(listenFd_) + 1,
                                       &readSet, nullptr, nullptr, &tv);
            if (stop_.load() || ready <= 0) continue;
            const net::Socket conn = ::accept(listenFd_, nullptr, nullptr);
            if (conn == net::invalidSocket) break;
            // 握手超时：对端可能永远不发 ClientHello（如客户端构造失败）
#ifdef _WIN32
            DWORD rcvTimeout = 2000;
            ::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&rcvTimeout),
                         sizeof(rcvTimeout));
#else
            timeval rcvTimeout{2, 0};
            ::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout,
                         sizeof(rcvTimeout));
#endif
            SSL* ssl = SSL_new(ctx_.get());
            if (!ssl) {
#ifdef _WIN32
                ::closesocket(conn);
#else
                ::close(conn);
#endif
                continue;
            }
            SSL_set_fd(ssl, static_cast<int>(conn));
            SSL_set_accept_state(ssl);
            const int acc = SSL_accept(ssl);
            if (acc != 1) {
                std::fprintf(stderr, "[fake-srv] SSL_accept=%d err=%d\n", acc,
                             SSL_get_error(ssl, acc));
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
#ifdef _WIN32
            ::closesocket(conn);
#else
            ::close(conn);
#endif
        }
    }

    net::SslCtx ctx_;
    net::Socket listenFd_ = net::invalidSocket;
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

// 尝试 startSecure 握手；返回是否成功建立并保持 TLS 连接
bool tryHandshake(std::uint16_t port, const std::string& caPem,
                  const std::string& serverName) {
    client::NetClient net;
    std::atomic<bool> closed{false};
    try {
        net.startSecure(
            "127.0.0.1", port, caPem, serverName,
            [](const cc::proto::Message&) {},
            [&] { closed = true; });
    } catch (const std::exception&) {
        return false;
    }
    // 握手在收发线程；成功以 handshakeDone 为准（服务器随后会关闭连接，
    // connected() 不适合作为判据）。
    for (int i = 0; i < 30 && !net.handshakeDone() && !closed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const bool ok = net.handshakeDone();
    net.stop();
    return ok;
}

}  // namespace

TEST_CASE("M2-01a 合法 CA + 正确主机名握手成功") {
    auto tls = net::makeMemoryServerTls();
    FakeTlsListener listener(std::move(tls.server), kBasePort);
    CHECK(tryHandshake(kBasePort, tls.caCertPem, "localhost"));
}

TEST_CASE("M2-01b 未知 CA 被拒绝") {
    auto tls = net::makeMemoryServerTls();
    auto rogue = net::makeMemoryServerTls();  // 另一个不相干 CA
    FakeTlsListener listener(std::move(tls.server), kBasePort + 1);
    CHECK_FALSE(tryHandshake(kBasePort + 1, rogue.caCertPem, "localhost"));
}

TEST_CASE("M2-01c 错误主机名被拒绝") {
    auto tls = net::makeMemoryServerTls();
    FakeTlsListener listener(std::move(tls.server), kBasePort + 2);
    CHECK_FALSE(tryHandshake(kBasePort + 2, tls.caCertPem, "evil.example"));
}

TEST_CASE("M2-01d 过期证书被拒绝") {
    auto expired = net::makeMemoryServerTls(-1);  // 有效期为过去
    FakeTlsListener listener(std::move(expired.server), kBasePort + 3);
    CHECK_FALSE(
        tryHandshake(kBasePort + 3, expired.caCertPem, "localhost"));
}

TEST_CASE("M2-01e 失败连接不发出任何业务帧") {
    auto tls = net::makeMemoryServerTls();
    FakeTlsListener listener(std::move(tls.server), kBasePort + 4);
    client::NetClient net;
    std::atomic<bool> gotMessage{false};
    try {
        net.startSecure(
            "127.0.0.1", kBasePort + 4, "BOGUS-CA", "localhost",
            [&](const cc::proto::Message&) { gotMessage = true; }, [] {});
    } catch (const std::exception&) {
        // TCP 层失败同样计入"未发出"
    }
    // 即使尝试发送，也会因未建立连接而入队失败
    CHECK_FALSE(
        net.send(cc::proto::makeRegister("x", "口令-abc123", "Q", "R", "S",
                                         "T")));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK_FALSE(gotMessage.load());
    net.stop();
}
