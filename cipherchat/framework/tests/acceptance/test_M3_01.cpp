#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "tls_fixture.hpp"
#include "ccfw/transport.hpp"

using namespace ccfw;
using namespace cc::proto;
TEST_CASE("M3-01 secure transport reassembles fragmented headers and multiple TLS records") {
    test::TlsWireServer server([](auto& server, SSL* ssl) {
        const auto first = encodeFrame(serialize(makeHelloResult(true)));
        server.write(ssl, std::string_view(first).substr(0, 3));
        server.write(ssl, std::string_view(first).substr(3, 2));
        server.write(ssl, std::string_view(first).substr(5));
        auto large = withV2(makeRecvMsg("large", "bob", "alice", "chat", std::string(256 * 1024, 'a'), 0));
        server.write(ssl, encodeFrame(serialize(large)) + encodeFrame(serialize(makeRecipientAck("id", "alice"))));
        server.hold();
    });
    TlsTransport client;
    REQUIRE(client.connect(server.options()));
    auto empty = client.poll(); REQUIRE(empty); CHECK_FALSE(empty.value());
    server.release();
    std::vector<Message> messages;
    REQUIRE(test::eventually([&] {
        auto result = client.poll();
        if (result && result.value()) messages.push_back(std::move(*result.value()));
        return messages.size() == 3;
    }));
    CHECK(messages[0].type == MsgType::HelloResult);
    CHECK(bodyStr(messages[1].body, "payload").size() == 256 * 1024);
    CHECK(messages[2].type == MsgType::RecipientAck);
    client.close(); client.close();
    CHECK_FALSE(client.poll());
}
TEST_CASE("M3-01 complete outgoing frames reach the TLS server") {
    std::atomic<bool> matched{false};
    test::TlsWireServer server([&](auto& server, SSL* ssl) {
        auto message = server.read(ssl);
        matched = message && message->msgId == "outgoing" && bodyStr(message->body, "payload").size() == 256 * 1024;
        server.hold();
    });
    TlsTransport client;
    REQUIRE(client.connect(server.options())); server.release();
    REQUIRE(client.send(withV2(makeSendMsg("outgoing", "bob", "chat", std::string(256 * 1024, 'a')))));
    REQUIRE(test::eventually([&] { return matched.load(); }));
}
TEST_CASE("M3-01 malformed and oversized frames produce an explicit protocol error") {
    std::string bad;
    SUBCASE("oversized length") { bad.assign({char(1), char(0), char(0), char(0)}); }
    SUBCASE("zero length") { bad.assign(4, '\0'); }
    SUBCASE("invalid JSON") { bad = encodeFrame("not-json"); }
    test::TlsWireServer server([&](auto& server, SSL* ssl) { server.write(ssl, bad); server.hold(); });
    TlsTransport client;
    REQUIRE(client.connect(server.options())); server.release();
    std::optional<Error> error;
    REQUIRE(test::eventually([&] { auto result = client.poll(); if (!result) error = result.error(); return error.has_value(); }));
    CHECK(error->code == ErrorCode::ProtocolError);
    client.close(); client.close();
}
TEST_CASE("M3-01 incoming queue is bounded and overflow is visible") {
    test::TlsWireServer server([](auto& server, SSL* ssl) {
        std::string all;
        for (int i = 0; i < 300; ++i) all += encodeFrame(serialize(makeRecipientAck(std::to_string(i), "alice")));
        server.write(ssl, all);
        server.read(ssl); // peer closes on overflow; hold server alive until that close is observed
    });
    TlsTransport client;
    REQUIRE(client.connect(server.options())); server.release();
    REQUIRE(test::eventually([&] { return server.finished.load(); }));
    std::size_t count = 0; std::optional<Error> error;
    for (int i = 0; i < 300; ++i) {
        auto result = client.poll();
        if (!result) { error = result.error(); break; }
        if (result.value()) ++count;
    }
    REQUIRE(error); CHECK(error->code == ErrorCode::ProtocolError);
    CHECK(count == 256);
}
TEST_CASE("M3-01 timeout and invalid service identity fail before sending business data") {
    SUBCASE("silent TLS endpoint") {
        test::TlsWireServer server([](auto&, SSL*) {}, false);
        TlsTransport client; auto options = server.options(); options.timeoutMs = 150;
        const auto started = std::chrono::steady_clock::now();
        auto result = client.connect(options);
        REQUIRE_FALSE(result); CHECK(result.error().code == ErrorCode::Timeout);
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
        CHECK_FALSE(client.send(makeHello()));
    }
    SUBCASE("wrong hostname") {
        test::TlsWireServer server([](auto&, SSL*) {});
        TlsTransport client; auto options = server.options(); options.expectedServerName = "wrong.invalid";
        auto result = client.connect(options);
        REQUIRE_FALSE(result); CHECK(result.error().code == ErrorCode::AuthenticationFailed);
        CHECK_FALSE(client.send(makeHello()));
    }
}
TEST_CASE("M3-01 one adapter reconnects without stale frames or stale closure state") {
    test::TlsWireServer server([](auto& server, SSL* ssl) { server.write(ssl, encodeFrame(serialize(makeHelloResult(true)))); });
    TlsTransport client; server.release();
    for (int i = 0; i < 4; ++i) {
        REQUIRE(client.connect(server.options()));
        bool received = false; bool disconnected = false;
        REQUIRE(test::eventually([&] {
            auto result = client.poll();
            if (result && result.value()) received = result.value()->type == MsgType::HelloResult;
            if (!result) disconnected = true;
            return received && disconnected;
        }));
        client.close(); client.close();
    }
}
