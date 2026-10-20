#include "ccfw/direct_file_channel.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <random>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t invalid_socket = -1;
#endif

namespace ccfw {
namespace {
constexpr std::uint32_t maxFrame = 256U * 1024U;
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
struct WsaInit { WsaInit() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("winsock"); } ~WsaInit() { WSACleanup(); } };
WsaInit& wsa() { static WsaInit value; return value; }
void closeSocket(socket_t s) { if (s != invalid_socket) closesocket(s); }
bool nonblocking(socket_t s) { u_long mode = 1; return ioctlsocket(s, FIONBIO, &mode) == 0; }
bool wouldBlock() { const auto e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
void closeSocket(socket_t s) { if (s != invalid_socket) ::close(s); }
bool nonblocking(socket_t s) { const auto flags = fcntl(s, F_GETFL, 0); return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0; }
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
#endif
struct Socket {
    socket_t value = invalid_socket;
    explicit Socket(socket_t s = invalid_socket) : value(s) {}
    ~Socket() { closeSocket(value); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};
Error channelError(ErrorCode code, const char* message) { return {code, "M8-01", message}; }
template<class T = Unit> Result<T> fail(ErrorCode code, const char* message) { return Result<T>::failure(channelError(code, message)); }
bool ready(socket_t s, bool writing) {
    fd_set set; FD_ZERO(&set); FD_SET(s, &set); timeval tv{0, 20000};
    return ::select(static_cast<int>(s) + 1, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &tv) > 0;
}
int writeSocket(socket_t s, const char* data, std::size_t size) {
#ifdef _WIN32
    return ::send(s, data, static_cast<int>(size), 0);
#else
    return static_cast<int>(::send(s, data, size, MSG_NOSIGNAL));
#endif
}
// Used only by the network worker, with a total deadline and cancellation.
bool exchangeBytes(socket_t s, char* data, std::size_t size, bool writing,
                   const std::atomic<bool>& stop, Clock::time_point deadline) {
    while (size && !stop && Clock::now() < deadline) {
        if (!ready(s, writing)) continue;
        const int count = writing ? writeSocket(s, data, size) : ::recv(s, data, static_cast<int>(size), 0);
        if (count < 0 && wouldBlock()) continue;
        if (count <= 0) return false;
        data += count; size -= static_cast<std::size_t>(count);
    }
    return size == 0;
}
std::string frame(const nlohmann::json& value) {
    const auto bytes = value.dump();
    if (bytes.empty() || bytes.size() > maxFrame) throw std::runtime_error("frame too large");
    std::string result(4, '\0');
    const auto size = static_cast<std::uint32_t>(bytes.size());
    result[0] = static_cast<char>(size >> 24); result[1] = static_cast<char>(size >> 16);
    result[2] = static_cast<char>(size >> 8); result[3] = static_cast<char>(size);
    result += bytes; return result;
}
nlohmann::json parseFrame(const std::string& bytes) {
    // Refuse nested input before building a DOM; frames have depth at most two.
    int depth = 0; bool quoted = false, escaped = false;
    for (char c : bytes) {
        if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 4) throw std::runtime_error("frame depth"); }
        else if (c == '}' || c == ']') --depth;
    }
    auto result = nlohmann::json::parse(bytes);
    if (!result.is_object()) throw std::runtime_error("frame object");
    return result;
}
std::uint32_t frameSize(const char* p) {
    auto b = reinterpret_cast<const unsigned char*>(p);
    const auto size = (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3];
    if (!size || size > maxFrame) throw std::runtime_error("frame length");
    return size;
}
bool readFrame(socket_t s, nlohmann::json& value, const std::atomic<bool>& stop, Clock::time_point deadline) {
    char header[4]{};
    if (!exchangeBytes(s, header, 4, false, stop, deadline)) return false;
    std::string payload(frameSize(header), '\0');
    if (!exchangeBytes(s, payload.data(), payload.size(), false, stop, deadline)) return false;
    value = parseFrame(payload); return true;
}
nlohmann::json envelopeJson(const WireEnvelope& e) {
    return {{"message_id", e.messageId}, {"from", e.from}, {"to", e.to}, {"kind", e.kind},
            {"payload", e.payloadBase64}, {"group_id", e.groupId}, {"timestamp", e.timestamp}};
}
WireEnvelope parseEnvelope(const nlohmann::json& j) {
    WireEnvelope e;
    e.messageId = j.at("message_id").get<std::string>(); e.from = j.at("from").get<std::string>();
    e.to = j.at("to").get<std::string>(); e.kind = j.at("kind").get<std::string>();
    e.payloadBase64 = j.at("payload").get<std::string>(); e.groupId = j.value("group_id", 0LL); e.timestamp = j.value("timestamp", 0LL);
    if (e.messageId.empty() || e.messageId.size() > 128 || e.kind.empty() || e.payloadBase64.size() > 128 * 1024) throw std::runtime_error("invalid envelope");
    return e;
}
std::vector<std::string> localHosts() {
    std::vector<std::string> result;
    char name[256]{}; if (gethostname(name, sizeof(name) - 1) != 0) return result;
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr; if (getaddrinfo(name, nullptr, &hints, &list) != 0) return result;
    for (auto* item = list; item && result.size() < 8; item = item->ai_next) {
        char text[INET6_ADDRSTRLEN]{};
        if (item->ai_family == AF_INET) {
            auto* address = reinterpret_cast<sockaddr_in*>(item->ai_addr);
            if (address->sin_addr.s_addr != 0 && (ntohl(address->sin_addr.s_addr) >> 24) != 127 && inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) result.emplace_back(text);
        } else if (item->ai_family == AF_INET6) {
            auto* address = reinterpret_cast<sockaddr_in6*>(item->ai_addr);
            if (!IN6_IS_ADDR_LOOPBACK(&address->sin6_addr) && !IN6_IS_ADDR_LINKLOCAL(&address->sin6_addr) && !IN6_IS_ADDR_UNSPECIFIED(&address->sin6_addr) && inet_ntop(AF_INET6, &address->sin6_addr, text, sizeof(text))) result.emplace_back(text);
        }
    }
    freeaddrinfo(list);
    std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}

struct DirectFileChannel::Impl {
    std::mutex mutex;
    std::deque<WireEnvelope> incoming;
    std::deque<std::string> outgoing;
    std::string fileId;
    std::atomic<bool> stop{false}, isConnected{false}, isFailed{false};
    std::thread worker;

    void markFailed() { isConnected = false; isFailed = true; }
    bool belongs(const WireEnvelope& e) const {
        return e.groupId == 0 && ((e.kind == "file_chunk" && e.messageId.starts_with("fchunk:" + fileId + ":")) ||
            (e.kind == "file_ack" && e.messageId.starts_with("fack:" + fileId + ":")));
    }
    void run(socket_t s) {
        isConnected = true;
        std::string input, output; std::size_t offset = 0;
        auto partialSince = Clock::now();
        while (!stop) {
            if (output.empty()) {
                std::lock_guard lock(mutex);
                if (!outgoing.empty()) { output = std::move(outgoing.front()); outgoing.pop_front(); offset = 0; }
            }
            fd_set reads, writes; FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(s, &reads);
            if (!output.empty()) FD_SET(s, &writes);
            timeval wait{0, 20000};
            if (::select(static_cast<int>(s) + 1, &reads, &writes, nullptr, &wait) < 0) throw std::runtime_error("select");
            if (FD_ISSET(s, &writes)) {
                const int count = writeSocket(s, output.data() + offset, output.size() - offset);
                if (count > 0) { offset += count; if (offset == output.size()) output.clear(); }
                else if (count == 0 || !wouldBlock()) throw std::runtime_error("send");
            }
            if (FD_ISSET(s, &reads)) {
                char block[65536]; const int count = ::recv(s, block, sizeof(block), 0);
                if (count > 0) {
                    if (input.empty()) partialSince = Clock::now();
                    input.append(block, count);
                    while (input.size() >= 4) {
                        const auto size = frameSize(input.data()); if (input.size() < size + 4) break;
                        auto message = parseFrame(input.substr(4, size)); input.erase(0, size + 4); partialSince = Clock::now();
                        if (message.value("type", std::string{}) != "envelope") throw std::runtime_error("type");
                        auto envelope = parseEnvelope(message.at("envelope"));
                        if (!belongs(envelope)) throw std::runtime_error("file binding");
                        std::lock_guard lock(mutex);
                        if (incoming.size() >= 64) throw std::runtime_error("receive queue full");
                        incoming.push_back(std::move(envelope));
                    }
                } else if (count == 0 || !wouldBlock()) throw std::runtime_error("recv");
            }
            if (!input.empty() && Clock::now() - partialSince > std::chrono::seconds(5)) throw std::runtime_error("partial frame timeout");
        }
    }
    void closeAll() {
        stop = true; isConnected = false;
        // Only the worker owns active sockets. Its cancellable waits are <=20ms.
        if (worker.joinable()) worker.join();
        std::lock_guard lock(mutex); incoming.clear(); outgoing.clear(); fileId.clear();
    }
};

DirectFileChannel::DirectFileChannel() : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    (void)wsa();
#endif
}
DirectFileChannel::~DirectFileChannel() { close(); }

Result<DirectFileEndpoint> DirectFileChannel::listen(const std::string& fileId, const std::string& token) {
    if (fileId.empty() || fileId.size() > 32 || token.size() < 16 || token.size() > 128) return fail<DirectFileEndpoint>(ErrorCode::InvalidArgument, "直连参数无效");
    close(); impl_->stop = false; impl_->isFailed = false; impl_->fileId = fileId;
    auto hosts = localHosts();
    if (hosts.empty()) return fail<DirectFileEndpoint>(ErrorCode::NotConnected, "未找到可用直连地址");
    socket_t listener = ::socket(AF_INET6, SOCK_STREAM, 0);
    bool ipv6 = listener != invalid_socket;
    if (ipv6) {
        int only = 0;
        if (setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&only), sizeof(only)) != 0) {
            closeSocket(listener); listener = invalid_socket; ipv6 = false;
        }
    }
    if (!ipv6) listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in6 bind6{}; bind6.sin6_family = AF_INET6;
    sockaddr_in bind4{}; bind4.sin_family = AF_INET;
    if (listener == invalid_socket || !nonblocking(listener) ||
        (ipv6 ? ::bind(listener, reinterpret_cast<sockaddr*>(&bind6), sizeof(bind6)) : ::bind(listener, reinterpret_cast<sockaddr*>(&bind4), sizeof(bind4))) != 0 ||
        ::listen(listener, 4) != 0) {
        closeSocket(listener); impl_->markFailed(); return fail<DirectFileEndpoint>(ErrorCode::NotConnected, "无法监听直连端口");
    }
    sockaddr_storage bound{}; socklen_t boundLen = sizeof(bound);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundLen) != 0) {
        closeSocket(listener); impl_->markFailed(); return fail<DirectFileEndpoint>(ErrorCode::NotConnected, "无法读取直连端口");
    }
    const auto port = ipv6 ? ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port) : ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
    if (!ipv6) std::erase_if(hosts, [](const auto& host) { return host.find(':') != std::string::npos; });
    impl_->worker = std::thread([p = impl_.get(), listener, fileId, token] {
        Socket listening(listener);
        const auto expiry = Clock::now() + std::chrono::seconds(30);
        try {
            while (!p->stop && Clock::now() < expiry) {
                if (!ready(listening.value, false)) continue;
                Socket accepted(::accept(listening.value, nullptr, nullptr));
                if (accepted.value == invalid_socket || !nonblocking(accepted.value)) continue;
                bool authenticated = false;
                try {
                    nlohmann::json hello; const auto deadline = Clock::now() + std::chrono::milliseconds(500);
                    authenticated = readFrame(accepted.value, hello, p->stop, deadline) && hello.value("type", std::string{}) == "hello" &&
                        hello.value("file_id", std::string{}) == fileId && hello.value("token", std::string{}) == token;
                    if (authenticated) { auto reply = frame({{"type", "ok"}}); authenticated = exchangeBytes(accepted.value, reply.data(), reply.size(), true, p->stop, deadline); }
                } catch (...) { authenticated = false; }
                if (!authenticated) continue; // Invalid/partial handshakes cannot kill the listener.
                closeSocket(listening.value); listening.value = invalid_socket;
                p->run(accepted.value); break;
            }
        } catch (...) {}
        p->markFailed();
    });
    return Result<DirectFileEndpoint>::success({fileId, std::move(hosts), port, token});
}

Result<> DirectFileChannel::connect(const DirectFileEndpoint& endpoint) {
    if (endpoint.fileId.empty() || endpoint.fileId.size() > 32 || endpoint.hosts.empty() || endpoint.hosts.size() > 8 || !endpoint.port || endpoint.token.size() < 16 || endpoint.token.size() > 128)
        return fail(ErrorCode::InvalidArgument, "直连端点无效");
    close(); impl_->stop = false; impl_->isFailed = false; impl_->fileId = endpoint.fileId;
    impl_->worker = std::thread([p = impl_.get(), endpoint] {
        try {
            for (const auto& host : endpoint.hosts) {
                if (p->stop) break;
                sockaddr_storage address{}; int family = AF_INET; socklen_t size = sizeof(sockaddr_in);
                auto* a4 = reinterpret_cast<sockaddr_in*>(&address); auto* a6 = reinterpret_cast<sockaddr_in6*>(&address);
                if (inet_pton(AF_INET, host.c_str(), &a4->sin_addr) == 1) { a4->sin_family = AF_INET; a4->sin_port = htons(endpoint.port); }
                else if (inet_pton(AF_INET6, host.c_str(), &a6->sin6_addr) == 1) { family = AF_INET6; size = sizeof(sockaddr_in6); a6->sin6_family = AF_INET6; a6->sin6_port = htons(endpoint.port); }
                else continue; // No DNS lookups from untrusted endpoint metadata.
                Socket socket(::socket(family, SOCK_STREAM, 0));
                if (socket.value == invalid_socket || !nonblocking(socket.value)) continue;
                const auto deadline = Clock::now() + std::chrono::milliseconds(350);
                const auto result = ::connect(socket.value, reinterpret_cast<sockaddr*>(&address), size);
                if (result != 0) {
                    if (!wouldBlock()) continue;
                    bool writable = false;
                    while (!p->stop && Clock::now() < deadline && !(writable = ready(socket.value, true))) {}
                    int error = 0; socklen_t errorSize = sizeof(error);
                    if (!writable || getsockopt(socket.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorSize) != 0 || error) continue;
                }
                auto hello = frame({{"type", "hello"}, {"file_id", endpoint.fileId}, {"token", endpoint.token}});
                nlohmann::json reply;
                if (!exchangeBytes(socket.value, hello.data(), hello.size(), true, p->stop, deadline) || !readFrame(socket.value, reply, p->stop, deadline) || reply.value("type", std::string{}) != "ok") continue;
                p->run(socket.value); break;
            }
        } catch (...) {}
        p->markFailed();
    });
    return Result<>::success({}); // Accepted asynchronously; inspect connected()/failed().
}
Result<> DirectFileChannel::send(const WireEnvelope& envelope) {
    if (!connected()) return fail(ErrorCode::NotConnected, "直连尚未建立");
    try {
        if (!impl_->belongs(envelope)) return fail(ErrorCode::ProtocolError, "直连文件标识不匹配");
        auto payload = frame({{"type", "envelope"}, {"envelope", envelopeJson(envelope)}});
        std::lock_guard lock(impl_->mutex);
        if (impl_->outgoing.size() >= 64) return fail(ErrorCode::Forbidden, "直连发送队列已满");
        impl_->outgoing.push_back(std::move(payload)); return Result<>::success({});
    } catch (...) { return fail(ErrorCode::ProtocolError, "直连数据帧无效"); }
}
Result<std::optional<WireEnvelope>> DirectFileChannel::poll() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->incoming.empty()) return Result<std::optional<WireEnvelope>>::success(std::nullopt);
    auto result = std::move(impl_->incoming.front()); impl_->incoming.pop_front();
    return Result<std::optional<WireEnvelope>>::success(std::move(result));
}
bool DirectFileChannel::connected() const noexcept { return impl_->isConnected.load(); }
bool DirectFileChannel::failed() const noexcept { return impl_->isFailed.load(); }
const std::string& DirectFileChannel::fileId() const noexcept { return impl_->fileId; }
void DirectFileChannel::close() noexcept { if (impl_) impl_->closeAll(); }
} // namespace ccfw
