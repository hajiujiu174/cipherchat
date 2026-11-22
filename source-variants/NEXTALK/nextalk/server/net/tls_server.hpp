#pragma once
// TCP + TLS 1.3 监听器：把 socket 连接适配成 Router::Peer，
// 每连接一线程，读帧 → Router；下行消息 → 帧 → SSL_write。
// WinSock 生命周期由本类管理（WSAStartup / WSACleanup）。

#include "common/net/tls_context.hpp"
#include "common/protocol/frame.hpp"
#include "common/protocol/messages.hpp"
#include "server/router/router.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <vector>

namespace cc::server {

class TlsServer {
public:
    // certPath/keyPath 为证书与私钥 PEM 路径（不存在时自动生成自签名）。
    TlsServer(Router& router, const std::string& certPath,
              const std::string& keyPath);
    ~TlsServer();

    // 阻塞监听直到 stop()。返回前回收全部连接线程。
    // bindAddress 接受 IPv4/IPv6 数字地址；"::" 启用双栈，库默认保持 IPv4 兼容。
    bool listen(std::uint16_t port, const std::string& bindAddress = "0.0.0.0");
    void stop();
    bool waitReady(std::uint32_t timeoutMs);
    std::uint16_t port() const noexcept { return boundPort_.load(); }

    void setLogger(std::function<void(const std::string&)> logger) {
        logger_ = std::move(logger);
    }

private:
    class TlsPeer;
    void serveConnection(std::shared_ptr<TlsPeer> peer);
    void log(const std::string& line);

    Router& router_;
    net::SslCtx sslCtx_;
    std::function<void(const std::string&)> logger_;
    std::mutex stateMutex_;               // 保护 peers_ / workers_ / stopping_
    std::vector<std::shared_ptr<TlsPeer>> peers_;
    struct Worker { std::thread thread; std::shared_ptr<std::atomic<bool>> done; };
    std::vector<Worker> workers_; // 已退出连接在监听循环回收
    bool stopping_ = false;
    std::atomic<std::uint16_t> boundPort_{0};
    std::condition_variable ready_;
    std::uint64_t socket_ = 0;  // 监听 socket（Winsock SOCKET）
};

}  // namespace cc::server
