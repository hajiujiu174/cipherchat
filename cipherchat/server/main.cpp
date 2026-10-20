// CipherChat 服务器入口。
// 用法: cipherchat-server [port] [db路径] [cert路径] [key路径] [监听IP]
//   port    默认 55220
//   db路径  默认 cipherchat-server.db
// 证书在首次运行时自动生成；默认 cert.pem / key.pem 位于当前工作目录。
#include "server/net/tls_server.hpp"
#include "server/router/router.hpp"
#include "server/storage/storage.hpp"
#include "server/smtp_mailer.hpp"

#include <cstdio>
#include <charconv>
#include <atomic>
#include <csignal>
#include <chrono>
#include <future>
#include <string>
#include <string_view>

namespace {

std::atomic<bool> stopRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void handleStop(int) { stopRequested.store(true, std::memory_order_relaxed); }

void consoleLog(const std::string& line) {
    std::fprintf(stderr, "%s\n", line.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 6 || (argc > 1 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h"))) {
        std::fprintf(stderr, "Usage: cipherchat-server [port:1..65535] [database] [certificate.pem] [key.pem] [bind-IP:default=::]\n");
        return argc > 6 ? 2 : 0;
    }
    unsigned int parsedPort = 55220;
    if (argc > 1) {
        const std::string_view input(argv[1]);
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), parsedPort);
        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || parsedPort == 0 || parsedPort > 65535) {
            std::fprintf(stderr, "Invalid port: expected an integer in 1..65535.\n"); return 2;
        }
    }
    const auto port = static_cast<std::uint16_t>(parsedPort);
    const std::string dbPath = argc > 2 ? argv[2] : "cipherchat-server.db";
    const std::string certPath = argc > 3 ? argv[3] : "cert.pem";
    const std::string keyPath = argc > 4 ? argv[4] : "key.pem";
    const std::string bindAddress = argc > 5 ? argv[5] : "::";

    std::signal(SIGINT, handleStop);
    std::signal(SIGTERM, handleStop);
#ifndef _WIN32
    // OpenSSL 的 socket BIO 在断开的连接上写入可能触发 SIGPIPE。
    std::signal(SIGPIPE, SIG_IGN);
#endif

    try {
        cc::server::Storage db(dbPath);
        cc::server::Router router(db);
        auto mail = std::make_shared<cc::server::RegistrationEmail>(cc::server::smtpSenderFromEnvironment());
        router.requireRegistrationEmail(mail);
        router.enablePasswordReset(std::make_shared<cc::server::RegistrationEmail>(cc::server::smtpSenderFromEnvironment(true)));
        router.enableEmailLogin(std::make_shared<cc::server::RegistrationEmail>(cc::server::smtpSenderFromEnvironment(false,true)));
        consoleLog(mail->available() ? "[server] Registration email verification enabled" : "[server] SMTP not configured: new registration unavailable; existing login remains available");
        cc::server::TlsServer tls(router, certPath, keyPath);
        tls.setLogger(&consoleLog);
        consoleLog("[server] CipherChat 普通聊天服务器启动（TLS 连接、密码验证、消息与文件中转）");
        auto listener = std::async(std::launch::async, [&] { return tls.listen(port, bindAddress); });
        while (listener.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            if (stopRequested.load(std::memory_order_relaxed)) tls.stop();
        }
        if (!listener.get()) return 1;
        consoleLog("[server] 已停止");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[server] 启动失败: %s\n", e.what());
        return 1;
    }
}
