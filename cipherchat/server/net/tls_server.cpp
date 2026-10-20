#include "common/net/socket.hpp"
#include "server/net/tls_server.hpp"
#include "server/update_feed.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <openssl/ssl.h>

#include <atomic>
#include <cstring>
#include <chrono>

namespace cc::server {

namespace {

constexpr std::size_t kRecvChunk = 16 * 1024;

#ifndef _WIN32
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif

void initWinsock() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

void cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void closeSocket(std::uint64_t s) {
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(s));
#else
    ::close(static_cast<int>(s));
#endif
}

}  // namespace

// ---- TlsPeer：一条 TLS 连接 ----

// 线程约定（与 framework/docs/contracts.md 一致）：同一 SSL 对象只允许一个
// 线程触碰。deliver()（可能来自其他连接的广播线程）仅入队出站帧，
// 由本连接的收发线程在 poll 空闲窗口统一 SSL_write。
class TlsServer::TlsPeer : public Peer, public std::enable_shared_from_this<TlsPeer> {
public:
    TlsPeer(TlsServer& server, SSL* ssl, std::uint64_t socket)
        : server_(server), ssl_(ssl), socket_(socket) {}
    ~TlsPeer() override;

    // Peer 接口
    void deliver(const cc::proto::Message& msg) override;
    const std::string& username() const override { return username_; }
    void setUsername(std::string name) override { username_ = std::move(name); }

    // 连接主循环（连接线程内执行）
    void run();
    void shutdown();

private:
    bool enqueueFrame(const std::string& payload);
    bool drainOutbox(bool update = false);
    bool waitReadable(int timeoutMs) const;

    TlsServer& server_;
    SSL* ssl_ = nullptr;
    std::uint64_t socket_;
    std::string username_;
    std::mutex outboxMutex_;
    std::deque<std::string> outbox_;
    std::size_t outboxBytes_ = 0;
    std::atomic<bool> closed_{false};
};

TlsServer::TlsPeer::~TlsPeer() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
    }
    closeSocket(socket_);
}

void TlsServer::TlsPeer::deliver(const cc::proto::Message& msg) {
    if (!enqueueFrame(cc::proto::serialize(msg))) shutdown();
}

bool TlsServer::TlsPeer::enqueueFrame(const std::string& payload) {
    if (closed_.load()) return false;
    const std::string frame = cc::proto::encodeFrame(payload);
    std::lock_guard<std::mutex> lock(outboxMutex_);
    if (outbox_.size() >= cc::net::kOutboxMaxFrames ||
        outboxBytes_ + frame.size() > cc::net::kOutboxMaxBytes) {
        return false;  // 触顶：受控失败
    }
    outboxBytes_ += frame.size();
    outbox_.push_back(std::move(frame));
    return true;
}

bool TlsServer::TlsPeer::drainOutbox(bool update) {
    std::deque<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(outboxMutex_);
        batch.swap(outbox_);
    }
    while (!batch.empty()) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(update ? 60 : 5);
        const std::string& frame = batch.front();
        std::size_t sent = 0;
        while (sent < frame.size()) {
            if (closed_) return false;
            const int n =
                SSL_write(ssl_, frame.data() + static_cast<int>(sent),
                          static_cast<int>(frame.size() - sent));
            if (n <= 0) {
                const auto error = SSL_get_error(ssl_, n);
                if ((error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) || std::chrono::steady_clock::now() >= deadline) {
                    if (update) server_.log("大块响应发送失败，TLS错误=" + std::to_string(error));
                    closed_ = true; return false;
                }
                cc::net::waitSocket(static_cast<cc::net::Socket>(socket_), error == SSL_ERROR_WANT_READ, error == SSL_ERROR_WANT_WRITE, 25);
                continue;
            }
            sent += static_cast<std::size_t>(n);
        }
        { std::lock_guard lock(outboxMutex_); outboxBytes_ -= frame.size(); }
        batch.pop_front();
    }
    return true;
}

bool TlsServer::TlsPeer::waitReadable(int timeoutMs) const {
    fd_set readSet;
    FD_ZERO(&readSet);
    const auto fd = static_cast<SOCKET>(socket_);
    FD_SET(fd, &readSet);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return ::select(static_cast<int>(fd) + 1, &readSet, nullptr, nullptr, &tv) > 0;
}

void TlsServer::TlsPeer::run() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!closed_) {
        const auto result = SSL_accept(ssl_);
        if (result == 1) break;
        const auto error = SSL_get_error(ssl_, result);
        if ((error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) || std::chrono::steady_clock::now() >= deadline) { closed_ = true; break; }
        cc::net::waitSocket(static_cast<cc::net::Socket>(socket_), error == SSL_ERROR_WANT_READ, error == SSL_ERROR_WANT_WRITE, 25);
    }
    std::string buffer;
    std::vector<char> chunk(kRecvChunk);
    while (!closed_.load()) {
        // 出站帧（含广播）统一在本线程写出
        if (!drainOutbox()) break;

        if (SSL_pending(ssl_) == 0 && !waitReadable(25)) continue;

        const int n = SSL_read(ssl_, chunk.data(), kRecvChunk);
        if (n <= 0) {
            // TLS 1.3 后握手指令（如 NewSessionTicket）处理完返回
            // WANT_READ，并非连接结束；真正的 EOF/错误才退出。
            const int err = SSL_get_error(ssl_, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            break;  // EOF 或 TLS 错误
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(n));

        // 解出全部完整帧
        while (true) {
            cc::proto::DecodedFrame frame;
            const auto rc = cc::proto::decodeFrame(buffer, frame);
            if (rc == cc::proto::DecodeResult::NeedMoreData) break;
            if (rc != cc::proto::DecodeResult::Ok) {
                server_.log("连接异常帧（TooLarge/Invalid），断开");
                enqueueFrame(cc::proto::serialize(
                    cc::proto::makeError("bad_frame", "帧格式非法")));
                shutdown();
                break;
            }
            buffer.erase(0, frame.consumed);
            auto msg = cc::proto::deserialize(frame.payload);
            if (!msg) {
                shutdown();
                break;
            }
            if (msg->type == cc::proto::MsgType::UpdateGet) {
                deliver(updateResponse(*msg));
                // Drain one bounded update response before accepting more requests.
                if (!drainOutbox(true)) { shutdown(); break; }
            } else {
                server_.router_.onMessage(*this, *msg);
                // Cached ciphertext windows are bulk responses, like update blocks.
                // Allow a slow public connection to drain before reading more requests.
                if (msg->type == cc::proto::MsgType::FileCache && !drainOutbox(true)) { shutdown(); break; }
            }
        }
    }
    shutdown();
    server_.router_.onDisconnect(*this);
    server_.log(username_.empty() ? "匿名连接断开"
                                  : ("用户 " + username_ + " 断开"));
    if (ssl_) { SSL_free(ssl_); ssl_ = nullptr; }
    closeSocket(socket_); socket_ = static_cast<std::uint64_t>(cc::net::invalidSocket);
}

void TlsServer::TlsPeer::shutdown() {
    closed_ = true; // 网络线程至多 25ms 后观察到并关闭自己的 TLS/socket。
}

// ---- TlsServer ----

TlsServer::TlsServer(Router& router, const std::string& certPath,
                     const std::string& keyPath)
    : router_(router), sslCtx_(net::makeServerContext(certPath, keyPath)) {
    initWinsock();
}

TlsServer::~TlsServer() {
    stop();
    cleanupWinsock();
}

void TlsServer::log(const std::string& line) {
    if (logger_) logger_("[server] " + line);
}

bool TlsServer::listen(std::uint16_t port, const std::string& bindAddress) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
    addrinfo* raw = nullptr;
    if (bindAddress.empty() || getaddrinfo(bindAddress.c_str(), std::to_string(port).c_str(), &hints, &raw) != 0) {
        log("无效的监听 IP 地址: " + bindAddress);
        return false;
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, &freeaddrinfo);
    SOCKET fd = INVALID_SOCKET;
    for (const auto* address = addresses.get(); address; address = address->ai_next) {
        const auto candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == INVALID_SOCKET) continue;
        int reuse = 1;
        ::setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (address->ai_family == AF_INET6) {
            int v6Only = 0;
            if (::setsockopt(candidate, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only)) != 0) {
                closeSocket(static_cast<std::uint64_t>(candidate));
                continue;
            }
        }
        if (::bind(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) { fd = candidate; break; }
        closeSocket(static_cast<std::uint64_t>(candidate));
    }
    if (fd == INVALID_SOCKET) {
        log("无法绑定监听地址 " + bindAddress + ":" + std::to_string(port));
        return false;
    }
    socket_ = static_cast<std::uint64_t>(fd);
    if (!cc::net::nonblocking(fd) || ::listen(fd, 16) != 0) {
        log("listen 失败");
        closeSocket(static_cast<std::uint64_t>(fd)); socket_ = 0;
        return false;
    }
    sockaddr_storage bound{};
#ifdef _WIN32
    int boundSize = sizeof(bound);
#else
    socklen_t boundSize = sizeof(bound);
#endif
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundSize) != 0) {
        log("无法读取监听端口");
        closeSocket(static_cast<std::uint64_t>(fd)); socket_ = 0;
        return false;
    }
    const auto actualPort = bound.ss_family == AF_INET6
        ? ntohs(reinterpret_cast<const sockaddr_in6*>(&bound)->sin6_port)
        : ntohs(reinterpret_cast<const sockaddr_in*>(&bound)->sin_port);
    { std::lock_guard lock(stateMutex_); boundPort_ = actualPort; }
    ready_.notify_all();
    log("TLS 1.3 监听 " + bindAddress + ":" + std::to_string(actualPort));

    while (true) {
        std::vector<std::thread> finished;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (stopping_) break;
            for (auto it = workers_.begin(); it != workers_.end();) {
                if (*it->done) { finished.push_back(std::move(it->thread)); it = workers_.erase(it); }
                else ++it;
            }
        }
        for (auto& worker : finished) if (worker.joinable()) worker.join();
        if (cc::net::waitSocket(fd, true, false, 25) <= 0) continue;
        sockaddr_storage client{};
#ifdef _WIN32
        int clientLen = sizeof(client);
#else
        socklen_t clientLen = sizeof(client);
#endif
        const SOCKET conn =
            ::accept(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&client),
                     &clientLen);
        if (conn == INVALID_SOCKET) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (stopping_) break;  // stop() 关闭了监听 socket
            continue;
        }
        { std::lock_guard lock(stateMutex_); if (peers_.size() >= 256) { closeSocket(static_cast<std::uint64_t>(conn)); continue; } }
        SSL* ssl = SSL_new(sslCtx_.get());
        if (!ssl) {
            closeSocket(static_cast<std::uint64_t>(conn));
            continue;
        }
        cc::net::nonblocking(conn);
        SSL_set_fd(ssl, static_cast<int>(conn));
        auto peer = std::make_shared<TlsPeer>(*this, ssl,
                                              static_cast<std::uint64_t>(conn));
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            peers_.push_back(peer);
            auto done = std::make_shared<std::atomic<bool>>(false);
            workers_.push_back({std::thread([this, peer, done] { serveConnection(peer); *done = true; }), done});
        }
    }

    // 回收连接：全部 shutdown 后逐一 join，确保无线程再触碰 Router/Storage
    std::vector<std::shared_ptr<TlsPeer>> remaining;
    std::vector<Worker> threads;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        remaining.swap(peers_);
        threads.swap(workers_);
    }
    for (auto& p : remaining) p->shutdown();
    for (auto& t : threads) {
        if (t.thread.joinable()) t.thread.join();
    }
    closeSocket(static_cast<std::uint64_t>(fd));
    socket_ = 0; boundPort_ = 0;
    return true;
}

void TlsServer::serveConnection(std::shared_ptr<TlsPeer> peer) {
    peer->run();
    // 清理 peers_ 中的引用；listen 循环定期回收已结束线程。
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->get() == peer.get()) {
            peers_.erase(it);
            break;
        }
    }
}

void TlsServer::stop() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    ready_.notify_all();
}

bool TlsServer::waitReady(std::uint32_t timeoutMs) {
    std::unique_lock lock(stateMutex_);
    return ready_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return stopping_ || boundPort_.load() != 0; }) && boundPort_ != 0;
}

}  // namespace cc::server
