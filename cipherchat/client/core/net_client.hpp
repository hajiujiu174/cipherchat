#pragma once
// TLS 客户端连接：连接服务器、后台收发线程、线程安全发送。
//
// 线程约定（与 framework/docs/contracts.md 一致：网络只收发并入队）：
// 同一 SSL 对象只允许一个线程触碰。send() 仅把序列化帧放入有界出站队列，
// 由收发线程在 poll 空闲窗口统一 SSL_write；并发读写同一 SSL* 会丢记录。
// 所有回调在收发线程触发；ClientCore 负责进一步的分发与加锁。

#include "common/net/tls_context.hpp"
#include "common/protocol/frame.hpp"
#include "common/protocol/messages.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#include <sys/socket.h>
#endif

namespace cc::client {

class NetClient {
public:
    using MessageHandler = std::function<void(const cc::proto::Message&)>;
    using CloseHandler = std::function<void()>;
    enum class CloseReason { None, Disconnected, AuthenticationFailed, ProtocolError, Timeout };

    ~NetClient();

    // 正式路径：验证服务器证书链（信任锚 caPem）与主机名（serverName，
    // 同时用于 SNI）。验证失败即连接失败。
    void startSecure(const std::string& host, std::uint16_t port,
                     const std::string& caPem, const std::string& serverName,
                     MessageHandler onMessage, CloseHandler onClose,
                     std::uint32_t timeoutMs = 5000);
    // 旧版回归路径（v1）：不校验证书。正式业务请使用 startSecure。
    void start(const std::string& host, std::uint16_t port,
               MessageHandler onMessage, CloseHandler onClose);
    void stop();

    bool connected() const { return running_.load(); }
    // TLS 握手是否已完成（GUI 可据此显示"已加密连接"）
    bool handshakeDone() const { return handshakeDone_.load(); }
    bool waitHandshake(std::uint32_t timeoutMs);
    CloseReason closeReason() const { return closeReason_.load(); }
    const std::string& username() const { return username_; }
    void setUsername(std::string name) { username_ = std::move(name); }

    // 线程安全发送：仅入队；断线或队列触顶返回 false
    bool send(const cc::proto::Message& msg);

private:
    void startCommon(const std::string& host, std::uint16_t port,
                     MessageHandler onMessage, CloseHandler onClose,
                     bool verifyPeer, const std::string& caPem,
                     const std::string& serverName, std::uint32_t timeoutMs);
    void readLoop();
    void notifyClosed();

    net::SslCtx sslCtx_{nullptr, net::SslCtxDeleter{}};
    SSL* ssl_ = nullptr;
    std::uint64_t socket_ = 0;
    std::thread reader_;
    std::atomic<bool> running_{false};
    std::atomic<bool> handshakeDone_{false};
    std::atomic<bool> closedNotified_{false};
    std::atomic<CloseReason> closeReason_{CloseReason::None};
    std::mutex handshakeMutex_;
    std::condition_variable handshakeCv_;
    std::uint32_t timeoutMs_ = 5000;
    // 出站帧队列：send() 生产，收发线程消费
    std::mutex outboxMutex_;
    std::deque<std::string> outbox_;
    std::size_t outboxBytes_ = 0;
    std::string username_;
    std::string host_;         // SNI 用；握手移交给收发线程后需在此持有
    std::string serverName_;   // 主机名校验（startSecure）
    bool verifyPeer_ = false;  // 正式路径标志
    MessageHandler onMessage_;
    CloseHandler onClose_;
};

}  // namespace cc::client
