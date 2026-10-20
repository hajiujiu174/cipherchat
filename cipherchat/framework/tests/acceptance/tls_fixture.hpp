#pragma once
#include "common/net/socket.hpp"
#include "common/net/tls_context.hpp"
#include "common/protocol/frame.hpp"
#include "common/protocol/messages.hpp"
#include "ccfw/types.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

namespace ccfw::test {
class TlsWireServer {
public:
    using Clock = std::chrono::steady_clock;
    using Script = std::function<void(TlsWireServer&, SSL*)>;
    explicit TlsWireServer(Script script, bool handshake = true) : script_(std::move(script)), handshake_(handshake) {
        cc::net::initializeSockets();
        tls_ = cc::net::makeMemoryServerTls();
        directory = std::filesystem::current_path() / ("tls-test-" + std::to_string(Clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "ca.pem") << tls_.caCertPem;
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == cc::net::invalidSocket) throw std::runtime_error("test listener creation failed");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || ::listen(listener_, 8))
            throw std::runtime_error("test listener bind failed");
#ifdef _WIN32
        int size = sizeof(address);
#else
        socklen_t size = sizeof(address);
#endif
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size); port = ntohs(address.sin_port);
        worker_ = std::thread([this] { run(); });
    }
    ~TlsWireServer() {
        stopping = true; condition_.notify_all();
        if (worker_.joinable()) worker_.join();
        cc::net::closeSocket(listener_);
    }
    ConnectionOptions options() const { return {"127.0.0.1", port, directory / "ca.pem", "localhost", 2000}; }
    void release() { { std::lock_guard lock(mutex_); enabled_ = true; } condition_.notify_all(); }
    bool write(SSL* ssl, std::string_view data) {
        const auto end = Clock::now() + std::chrono::seconds(3);
        std::size_t offset = 0;
        while (!stopping && offset < data.size() && Clock::now() < end) {
            const int count = SSL_write(ssl, data.data() + offset, static_cast<int>(data.size() - offset));
            if (count > 0) { offset += count; continue; }
            const auto error = SSL_get_error(ssl, count);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return false;
            cc::net::waitSocket(static_cast<cc::net::Socket>(SSL_get_fd(ssl)), error == SSL_ERROR_WANT_READ, error == SSL_ERROR_WANT_WRITE, 10);
        }
        return offset == data.size();
    }
    std::optional<cc::proto::Message> read(SSL* ssl) {
        const auto end = Clock::now() + std::chrono::seconds(3);
        std::string buffer; char bytes[4096];
        while (!stopping && Clock::now() < end) {
            const auto count = SSL_read(ssl, bytes, sizeof(bytes));
            if (count > 0) {
                buffer.append(bytes, count);
                cc::proto::DecodedFrame frame;
                if (cc::proto::decodeFrame(buffer, frame) == cc::proto::DecodeResult::Ok) return cc::proto::deserialize(frame.payload);
            } else {
                const auto error = SSL_get_error(ssl, count);
                if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) break;
                cc::net::waitSocket(static_cast<cc::net::Socket>(SSL_get_fd(ssl)), true, error == SSL_ERROR_WANT_WRITE, 10);
            }
        }
        return std::nullopt;
    }
    void hold() { std::unique_lock lock(mutex_); condition_.wait_for(lock, std::chrono::seconds(3), [this] { return stopping.load(); }); }
    std::filesystem::path directory;
    std::uint16_t port = 0;
    std::atomic<bool> stopping{false}, finished{false};
private:
    void run() {
        while (!stopping) {
            if (cc::net::waitSocket(listener_, true, false, 10) <= 0) continue;
            const auto socket = ::accept(listener_, nullptr, nullptr);
            if (socket == cc::net::invalidSocket) continue;
            cc::net::nonblocking(socket);
            SSL* ssl = SSL_new(tls_.server.get()); SSL_set_fd(ssl, static_cast<int>(socket));
            bool connected = false;
            const auto deadline = Clock::now() + std::chrono::seconds(3);
            while (!stopping && Clock::now() < deadline) {
                if (!handshake_) { hold(); break; }
                const auto result = SSL_accept(ssl);
                if (result == 1) { connected = true; break; }
                const auto error = SSL_get_error(ssl, result);
                if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) break;
                cc::net::waitSocket(socket, error == SSL_ERROR_WANT_READ, error == SSL_ERROR_WANT_WRITE, 10);
            }
            if (connected) {
                { std::unique_lock lock(mutex_); condition_.wait(lock, [this] { return enabled_ || stopping; }); }
                if (!stopping) { try { script_(*this, ssl); } catch (...) {} }
            }
            finished = true;
            SSL_free(ssl); cc::net::closeSocket(socket);
        }
    }
    cc::net::MemoryServerTls tls_;
    Script script_;
    bool handshake_;
    cc::net::Socket listener_ = cc::net::invalidSocket;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool enabled_ = false;
};
template<class Predicate> bool eventually(Predicate predicate, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::yield();
    }
    return predicate();
}
} // namespace ccfw::test
