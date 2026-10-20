#include "common/net/socket.hpp"
#include "client/core/net_client.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

namespace cc::client {
using Clock = std::chrono::steady_clock;
NetClient::~NetClient() { stop(); }
void NetClient::startSecure(const std::string& host, std::uint16_t port,
    const std::string& caPem, const std::string& serverName, MessageHandler message,
    CloseHandler close, std::uint32_t timeoutMs) {
    startCommon(host, port, std::move(message), std::move(close), true, caPem, serverName, timeoutMs);
}
void NetClient::start(const std::string& host, std::uint16_t port, MessageHandler message, CloseHandler close) {
    startCommon(host, port, std::move(message), std::move(close), false, {}, {}, 5000);
}
void NetClient::startCommon(const std::string& host, std::uint16_t port,
    MessageHandler message, CloseHandler close, bool verifyPeer,
    const std::string& caPem, const std::string& serverName, std::uint32_t timeoutMs) {
    stop();
    handshakeDone_ = false;
    if (host.empty() || !port || !timeoutMs || timeoutMs > 60000) throw std::runtime_error("连接参数非法");
    net::initializeSockets();
    sslCtx_ = verifyPeer ? net::makeClientContextFromPem(caPem, serverName) : net::makeClientContextInsecure();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* rawAddresses = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &rawAddresses) || !rawAddresses)
        throw std::runtime_error("地址解析失败");
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(rawAddresses, &freeaddrinfo);
    net::Socket socket = net::invalidSocket;
    for (auto* address = addresses.get(); address && Clock::now() < deadline; address = address->ai_next) {
        socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == net::invalidSocket) continue;
        bool connected = false;
        if (net::setIpv6StreamMtu(socket, address->ai_addr) && net::nonblocking(socket)) {
            if (::connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) connected = true;
            else {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                if (remaining > 0 && net::waitSocket(socket, false, true, static_cast<int>(remaining)) > 0) {
                    int error = 0;
#ifdef _WIN32
                    int length = sizeof(error);
#else
                    socklen_t length = sizeof(error);
#endif
                    connected = getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0 && !error;
                }
            }
        }
        if (connected) break;
        net::closeSocket(socket); socket = net::invalidSocket;
    }
    if (socket == net::invalidSocket) throw std::runtime_error("无法连接服务器或连接超时");
    socket_ = static_cast<std::uint64_t>(socket);
    host_ = host; serverName_ = serverName; verifyPeer_ = verifyPeer; timeoutMs_ = timeoutMs;
    onMessage_ = std::move(message); onClose_ = std::move(close);
    handshakeDone_ = false; closedNotified_ = false; closeReason_ = CloseReason::None;
    { std::lock_guard lock(outboxMutex_); outbox_.clear(); outboxBytes_ = 0; }
    running_ = true;
    try { reader_ = std::thread([this] { readLoop(); }); }
    catch (...) { running_ = false; net::closeSocket(socket); socket_ = 0; throw; }
}
bool NetClient::waitHandshake(std::uint32_t timeoutMs) {
    std::unique_lock lock(handshakeMutex_);
    handshakeCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
        return handshakeDone_.load() || !running_.load();
    });
    return handshakeDone_.load();
}
void NetClient::stop() {
    running_ = false;
    // 回调可请求停止，资源仅在网络线程退出后由外部 owner 回收。
    if (reader_.joinable() && reader_.get_id() == std::this_thread::get_id()) return;
    if (reader_.joinable()) reader_.join();
    sslCtx_.reset();
    { std::lock_guard lock(outboxMutex_); outbox_.clear(); outboxBytes_ = 0; }
}
void NetClient::notifyClosed() {
    { std::lock_guard lock(handshakeMutex_); }
    handshakeCv_.notify_all();
    if (!closedNotified_.exchange(true) && onClose_) onClose_();
}
bool NetClient::send(const cc::proto::Message& message) {
    if (!running_) return false;
    const auto frame = cc::proto::encodeFrame(cc::proto::serialize(message));
    std::lock_guard lock(outboxMutex_);
    if (!running_ || outbox_.size() >= net::kOutboxMaxFrames || outboxBytes_ + frame.size() > net::kOutboxMaxBytes) return false;
    outbox_.push_back(frame); outboxBytes_ += frame.size();
    return true;
}
void NetClient::readLoop() {
    const auto socket = static_cast<net::Socket>(socket_);
    try {
        ssl_ = SSL_new(sslCtx_.get());
        if (!ssl_ || SSL_set_fd(ssl_, static_cast<int>(socket)) != 1) throw std::runtime_error("TLS creation failed");
        if (verifyPeer_) {
            unsigned char ip[16];
            const bool numeric = inet_pton(AF_INET, serverName_.c_str(), ip) == 1 || inet_pton(AF_INET6, serverName_.c_str(), ip) == 1;
            if ((numeric ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl_), serverName_.c_str()) : SSL_set1_host(ssl_, serverName_.c_str())) != 1)
                throw std::runtime_error("TLS name verification failed");
        }
        if (SSL_set_tlsext_host_name(ssl_, (verifyPeer_ ? serverName_ : host_).c_str()) != 1) throw std::runtime_error("TLS SNI failed");
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs_);
        while (running_ && !handshakeDone_) {
            const auto result = SSL_connect(ssl_);
            if (result == 1) {
                { std::lock_guard lock(handshakeMutex_); handshakeDone_ = true; }
                handshakeCv_.notify_all(); break;
            }
            const auto error = SSL_get_error(ssl_, result);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                closeReason_ = CloseReason::AuthenticationFailed; break;
            }
            if (Clock::now() >= deadline) { closeReason_ = CloseReason::Timeout; break; }
            net::waitSocket(socket, error == SSL_ERROR_WANT_READ, error == SSL_ERROR_WANT_WRITE, 25);
        }
        std::string buffer, pendingWrite;
        std::size_t offset = 0;
        std::vector<char> chunk(16 * 1024);
        auto writeStarted = Clock::now();
        while (running_ && handshakeDone_) {
            if (pendingWrite.empty()) {
                std::lock_guard lock(outboxMutex_);
                if (!outbox_.empty()) {
                    pendingWrite = std::move(outbox_.front()); outbox_.pop_front();
                    // Keep the active frame charged until it has been completely sent.
                    offset = 0; writeStarted = Clock::now();
                }
            }
            bool wantWrite = false;
            if (!pendingWrite.empty()) {
                const auto count = SSL_write(ssl_, pendingWrite.data() + offset, static_cast<int>(pendingWrite.size() - offset));
                if (count > 0) {
                    offset += static_cast<std::size_t>(count);
                    if (offset == pendingWrite.size()) {
                        std::lock_guard lock(outboxMutex_);
                        outboxBytes_ -= pendingWrite.size(); pendingWrite.clear();
                    }
                } else {
                    const auto error = SSL_get_error(ssl_, count);
                    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) break;
                    wantWrite = error == SSL_ERROR_WANT_WRITE;
                    if (Clock::now() - writeStarted > std::chrono::milliseconds(timeoutMs_)) { closeReason_ = CloseReason::Timeout; break; }
                }
            }
            const auto count = SSL_read(ssl_, chunk.data(), static_cast<int>(chunk.size()));
            if (count > 0) {
                buffer.append(chunk.data(), static_cast<std::size_t>(count));
                for (;;) {
                    cc::proto::DecodedFrame frame;
                    const auto decoded = cc::proto::decodeFrame(buffer, frame);
                    if (decoded == cc::proto::DecodeResult::NeedMoreData) break;
                    if (decoded != cc::proto::DecodeResult::Ok) { closeReason_ = CloseReason::ProtocolError; running_ = false; break; }
                    buffer.erase(0, frame.consumed);
                    auto message = cc::proto::deserialize(frame.payload);
                    if (!message) { closeReason_ = CloseReason::ProtocolError; running_ = false; break; }
                    if (onMessage_) onMessage_(*message);
                    if (!running_) break;
                }
                continue;
            }
            const auto error = SSL_get_error(ssl_, count);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) break;
            net::waitSocket(socket, true, wantWrite || error == SSL_ERROR_WANT_WRITE, 25);
        }
    } catch (const std::exception&) {
        if (closeReason_ == CloseReason::None) closeReason_ = CloseReason::Disconnected;
    }
    running_ = false;
    if (closeReason_ == CloseReason::None) closeReason_ = CloseReason::Disconnected;
    if (ssl_) { SSL_free(ssl_); ssl_ = nullptr; }
    net::closeSocket(socket); socket_ = 0;
    notifyClosed();
}
} // namespace cc::client
