#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "common/net/socket.hpp"
#include "file_fixture.hpp"
#include <chrono>
#include <thread>
using namespace ccfw;
namespace {
template<class F> bool until(F predicate, int ms = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while (std::chrono::steady_clock::now() < end);
    return false;
}
std::string offer(test::FileClient& a, test::FileClient& b, const cc::Bytes& data) {
    a.trust(b); b.trust(a);
    const auto source = a.directory / "hybrid.bin"; test::writeBytes(source, data);
    const auto started = a.files.start("bob", source); REQUIRE(started);
    for (std::size_t i = 0; i < data.size() / (4 * 65536) + 2; ++i) REQUIRE(a.files.tick());
    auto queued = a.store.load().value(); REQUIRE(!queued.pendingSends.empty());
    auto plain = queued.pendingSends.front();
    auto body = nlohmann::json::parse(plain.plaintext);
    // Same-process fixture uses an explicit loopback endpoint, never advertised in production.
    if (body.contains("direct")) body["direct"]["hosts"] = {"127.0.0.1"};
    plain.plaintext = body.dump();
    REQUIRE(a.sessions.establish("bob", wire::bundle(b.bundle())));
    auto encrypted = a.sessions.encryptAndCommit(plain); REQUIRE(encrypted);
    REQUIRE(b.files.receiveMetadata(encrypted.value()));
    a.net.sent.clear(); b.net.sent.clear();
    return started.value();
}
int relay(test::FileClient& from, test::FileClient& to) {
    auto messages = std::move(from.net.sent); from.net.sent.clear(); int chunks = 0;
    for (auto m : messages) {
        const auto kind = m.body.value("kind", std::string{});
        if (kind != "file_chunk" && kind != "file_ack") continue;
        m.body["from"] = from.store.load().value().account.username;
        const auto envelope = wire::envelope(m);
        if (kind == "file_chunk") { ++chunks; REQUIRE(to.files.receiveChunk(envelope)); }
        else REQUIRE(to.files.receiveAck(envelope));
    }
    return chunks;
}
int pump(test::FileClient& a, test::FileClient& b) {
    REQUIRE(a.files.tick()); REQUIRE(b.files.tick());
    return relay(a, b) + relay(b, a);
}
void completed(test::FileClient& a, test::FileClient& b, const std::string& id, const cc::Bytes& data, int& relayChunks) {
    REQUIRE(until([&] {
        relayChunks += pump(a, b);
        return a.files.progress(id).value().complete && b.files.progress(id).value().complete;
    }, 12000));
    CHECK(test::readBytes(b.files.progress(id).value().path) == data);
}
}
TEST_CASE("M8-01 IPv4 and IPv6 direct sockets authenticate and stay alive while idle") {
    for (const std::string host : {"127.0.0.1", "::1"}) {
        INFO(host); DirectFileChannel sender, peer;
        auto endpoint = sender.listen("loopback", "0123456789abcdef0123456789abcdef"); REQUIRE(endpoint);
        endpoint.value().hosts = {host}; REQUIRE(peer.connect(endpoint.value()));
        REQUIRE(until([&] { return sender.connected() && peer.connected(); }));
        if (host == "127.0.0.1") std::this_thread::sleep_for(std::chrono::milliseconds(5100));
        REQUIRE(peer.connected()); REQUIRE(sender.connected());
        WireEnvelope packet{"fchunk:loopback:0", "alice", "bob", "file_chunk", "encrypted", 0, 123};
        REQUIRE(peer.send(packet));
        std::optional<WireEnvelope> received;
        REQUIRE(until([&] { auto p = sender.poll(); REQUIRE(p); received = p.value(); return received.has_value(); }));
        CHECK(received->payloadBase64 == packet.payloadBase64);
        packet.messageId = "fchunk:another-file:0"; CHECK_FALSE(peer.send(packet));
    }
}
TEST_CASE("M8-01 wrong tokens and oversized headers cannot kill the listener") {
    DirectFileChannel sender, wrong, good;
    auto endpoint = sender.listen("auth", "0123456789abcdef0123456789abcdef"); REQUIRE(endpoint); endpoint.value().hosts = {"127.0.0.1"};
    auto bad = endpoint.value(); bad.token = "incorrect-token-123456789"; REQUIRE(wrong.connect(bad));
    REQUIRE(until([&] { return wrong.failed(); })); CHECK_FALSE(sender.connected()); CHECK_FALSE(sender.failed());
    const auto socket = ::socket(AF_INET, SOCK_STREAM, 0); REQUIRE(socket != cc::net::invalidSocket);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(endpoint.value().port); inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    REQUIRE(::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    const char header[] = "\xff\xff\xff\xff"; REQUIRE(::send(socket, header, 4, 0) == 4); cc::net::closeSocket(socket);
    REQUIRE(good.connect(endpoint.value())); REQUIRE(until([&] { return good.connected() && sender.connected(); }));
}
TEST_CASE("M8-01 cancellation of unreachable connect returns promptly") {
    DirectFileChannel peer;
    const auto before = std::chrono::steady_clock::now();
    REQUIRE(peer.connect({"unreachable", {"192.0.2.1", "2001:db8::1"}, 49152, "0123456789abcdef0123456789abcdef"}));
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(250));
    peer.close(); CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(500));
}
TEST_CASE("M8-01 actual encrypted file completes over P2P without Relay chunks") {
    test::FileClient a("alice", {}, true), b("bob", {}, true);
    const auto data = cc::crypto::randomBytes(16 * 65536 + 17); const auto id = offer(a, b, data); int chunks = 0;
    completed(a, b, id, data, chunks); CHECK(chunks == 0);
    CHECK(a.files.progress(id).value().transportPath == 2); CHECK(b.files.progress(id).value().transportPath == 2);
}
TEST_CASE("M8-01 unavailable direct peer falls back and completes the entire file") {
    test::FileClient a("alice", {}, true), b("bob");
    const auto data = cc::crypto::randomBytes(8 * 65536 + 23); const auto id = offer(a, b, data); int chunks = 0;
    completed(a, b, id, data, chunks); CHECK(chunks > 0);
    CHECK(a.files.progress(id).value().transportPath == 3); CHECK(b.files.progress(id).value().transportPath == 3);
}
TEST_CASE("M8-01 interruption after verified P2P bytes completes via Relay without changing keys") {
    test::FileClient a("alice", {}, true), b("bob", {}, true);
    const auto data = cc::crypto::randomBytes(32 * 65536 + 31); const auto id = offer(a, b, data); int chunks = 0;
    REQUIRE(until([&] { chunks += pump(a, b); return b.files.progress(id).value().authenticatedBytes >= 4 * 65536; }));
    REQUIRE(chunks == 0); REQUIRE_FALSE(b.files.progress(id).value().complete);
    const auto key = a.store.load().value().transfers.front().fileKey;
    a.files.setDirectEnabled(false); // Same operation as the user disabling P2P during transfer.
    completed(a, b, id, data, chunks); CHECK(chunks > 0);
    CHECK(a.files.progress(id).value().transportPath == 3); CHECK(a.store.load().value().transfers.front().fileKey == key);
}
TEST_CASE("M8-01 restarted sender resumes a P2P checkpoint through Relay") {
    test::FileClient b("bob", {}, true); const auto directory = test::tempDirectory("hybrid-restart");
    const auto data = cc::crypto::randomBytes(32 * 65536 + 37); std::string id; int chunks = 0; cc::Bytes key;
    {
        test::FileClient a("alice", directory, true); id = offer(a, b, data);
        REQUIRE(until([&] { chunks += pump(a, b); return b.files.progress(id).value().authenticatedBytes >= 4 * 65536; }));
        REQUIRE(chunks == 0); REQUIRE_FALSE(a.files.progress(id).value().complete);
        key = a.store.load().value().transfers.front().fileKey;
    }
    test::FileClient restored("alice", directory, true);
    completed(restored, b, id, data, chunks); CHECK(chunks > 0);
    CHECK(restored.files.progress(id).value().transportPath == 3); CHECK(restored.store.load().value().transfers.front().fileKey == key);
}
TEST_CASE("M8-01 Relay preference creates no direct listener and survives full file transfer") {
    test::FileClient a("alice", {}, true), b("bob", {}, true);
    a.files.setDirectEnabled(false); b.files.setDirectEnabled(false);
    const auto data = cc::crypto::randomBytes(8 * 65536 + 9); const auto id = offer(a, b, data); int chunks = 0;
    CHECK(a.direct->fileId().empty()); CHECK_FALSE(a.store.load().value().transfers.front().directEndpoint);
    completed(a, b, id, data, chunks); CHECK(chunks > 0); CHECK(a.files.progress(id).value().transportPath == 0);
}
TEST_CASE("M8-01 concurrent transfers cannot replace an active direct file channel") {
    test::FileClient a("alice", {}, true), b("bob", {}, true);
    const auto data = cc::crypto::randomBytes(65536); const auto first = offer(a, b, data);
    auto source = a.directory / "second.bin"; test::writeBytes(source, data);
    const auto second = a.files.start("bob", source); REQUIRE(second); REQUIRE(a.files.tick());
    CHECK(a.direct->fileId() == first); CHECK(a.files.progress(second.value()).value().transportPath == 0);
    REQUIRE(a.files.cancel(first)); CHECK(a.direct->fileId().empty());
}
