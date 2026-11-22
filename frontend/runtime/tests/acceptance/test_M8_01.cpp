#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include <chrono>
#include <thread>
using namespace ccfw;
namespace {
// 获取测试对应的 until，供调用方读取或参与计算。
template<class F> bool until(F predicate, int ms = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while (std::chrono::steady_clock::now() < end);
    return false;
}
// 验证“offer”场景中的关键结果，断言失败时由测试框架报告。
std::string offer(test::FileClient& a, test::FileClient& b, const cc::Bytes& data) {
    const auto source = a.directory / "hybrid.bin"; test::writeBytes(source, data);
    const auto started = a.files.start("bob", source); REQUIRE(started);
    for (std::size_t i = 0; i < data.size() / (4 * 65536) + 2; ++i) REQUIRE(a.files.tick());
    const auto queued = a.store.load().value();
    REQUIRE(queued.outbox.size() == 1);
    REQUIRE(b.files.receiveMetadata(queued.outbox.front()));
    a.net.sent.clear(); b.net.sent.clear();
    return started.value();
}
// 验证“中转”场景中的关键结果，断言失败时由测试框架报告。
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
// 验证“pump”场景中的关键结果，断言失败时由测试框架报告。
int pump(test::FileClient& a, test::FileClient& b) {
    REQUIRE(a.files.tick()); REQUIRE(b.files.tick());
    return relay(a, b) + relay(b, a);
}
// 验证“completed”场景中的关键结果，断言失败时由测试框架报告。
void completed(test::FileClient& a, test::FileClient& b, const std::string& id, const cc::Bytes& data, int& relayChunks) {
    REQUIRE(until([&] {
        relayChunks += pump(a, b);
        return a.files.progress(id).value().complete && b.files.progress(id).value().complete;
    }, 12000));
    CHECK(test::readBytes(b.files.progress(id).value().path) == data);
}
}
// 验证用例“M8-01 relays every chunk and validates the final bytes”所描述的行为。
TEST_CASE("M8-01 relays every chunk and validates the final bytes") {
    test::FileClient a("alice"), b("bob");
    const auto data = cc::crypto::randomBytes(8 * 65536 + 9);
    const auto id = offer(a, b, data);
    int chunks = 0;
    completed(a, b, id, data, chunks);
    CHECK(chunks == 9);
    CHECK(a.files.progress(id).value().transportPath == 0);
}

// 验证用例“M8-01 cancelling a relay transfer leaves another transfer active”所描述的行为。
TEST_CASE("M8-01 cancelling a relay transfer leaves another transfer active") {
    test::FileClient a("alice"), b("bob");
    const auto data = cc::crypto::randomBytes(65536);
    const auto first = offer(a, b, data);
    const auto source = a.directory / "second.bin";
    test::writeBytes(source, data);
    const auto second = a.files.start("bob", source); REQUIRE(second);
    REQUIRE(a.files.cancel(first));
    REQUIRE(a.files.tick());
    const auto state = a.store.load().value();
    CHECK(state.transfers.size() == 2);
    CHECK(a.files.progress(first).value().cancelled);
    CHECK(a.files.progress(second.value()).value().transportPath == 0);
}

// 验证用例“M8-01 old checkpoint route values resume through the server”所描述的行为。
TEST_CASE("M8-01 old checkpoint route values resume through the server") {
    test::FileClient a("alice"), b("bob");
    const auto data = cc::crypto::randomBytes(5 * 65536 + 10);
    const auto id = offer(a, b, data);
    const auto transfer = a.store.load().value().transfers.front();
    auto legacy = nlohmann::json(transfer);
    legacy["transport_path"] = 2;
    legacy["direct"] = {{"hosts", {"127.0.0.1"}}, {"token", "retired"}};
    CHECK(legacy.get<TransferCheckpoint>().path == 3);
    CHECK(nlohmann::json(legacy.get<TransferCheckpoint>()).count("direct") == 0);
    int chunks = 0;
    completed(a, b, id, data, chunks);
    CHECK(chunks == 6);
}
