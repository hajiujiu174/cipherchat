#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "client/core/net_client.hpp"
#include "server/net/tls_server.hpp"
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>

namespace {
struct Server {
    std::filesystem::path directory = std::filesystem::current_path() /
        ("network-addresses-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    cc::server::Storage database{""};
    cc::server::Router router{database};
    std::unique_ptr<cc::server::TlsServer> tls;
    std::future<bool> worker;
    explicit Server(const std::string& address) {
        std::filesystem::create_directories(directory);
        tls = std::make_unique<cc::server::TlsServer>(router, (directory / "cert.pem").string(), (directory / "key.pem").string());
        worker = std::async(std::launch::async, [this, address] { return tls->listen(0, address); });
    }
    ~Server() { tls->stop(); if (worker.valid()) worker.wait(); }
    std::string certificate() const {
        std::ifstream input(directory / "cert.pem"); return {std::istreambuf_iterator<char>(input), {}};
    }
};
void verifiedHello(Server& server, const std::string& host) {
    std::mutex mutex;
    std::condition_variable ready;
    bool negotiated = false;
    cc::client::NetClient client;
    client.startSecure(host, server.tls->port(), server.certificate(), "localhost",
        [&](const cc::proto::Message& reply) {
            std::lock_guard lock(mutex);
            negotiated = reply.type == cc::proto::MsgType::HelloResult && cc::proto::bodyBool(reply.body, "ok");
            ready.notify_all();
        }, [] {});
    REQUIRE(client.waitHandshake(3000));
    REQUIRE(client.send(cc::proto::makeHello()));
    { std::unique_lock lock(mutex); REQUIRE(ready.wait_for(lock, std::chrono::seconds(3), [&] { return negotiated; })); }
    client.stop();
}
}
TEST_CASE("IPv6 loopback completes verified TLS 1.3 and v2 negotiation") {
    Server server("::1"); REQUIRE(server.tls->waitReady(3000)); verifiedHello(server, "::1");
}
TEST_CASE("IPv6 wildcard listener accepts both IPv6 and IPv4 clients") {
    Server server("::"); REQUIRE(server.tls->waitReady(3000));
    verifiedHello(server, "::1"); verifiedHello(server, "127.0.0.1");
}
TEST_CASE("IPv4 explicit binding remains compatible") {
    Server server("127.0.0.1"); REQUIRE(server.tls->waitReady(3000)); verifiedHello(server, "127.0.0.1");
}
TEST_CASE("Invalid bind address fails without claiming a listening port") {
    Server server("not-an-ip"); REQUIRE(server.worker.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK_FALSE(server.worker.get()); CHECK(server.tls->port() == 0);
}
int main(int argc, char** argv) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    doctest::Context context(argc, argv); return context.run();
}
