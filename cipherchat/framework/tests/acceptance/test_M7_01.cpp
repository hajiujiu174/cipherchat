#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
using namespace ccfw;
namespace {
std::string ready(test::FileClient& alice, test::FileClient& bob, std::size_t bytes) {
    alice.trust(bob); bob.trust(alice);
    const auto path = alice.directory / "window-source.bin"; test::writeBytes(path, cc::Bytes(bytes, 'w'));
    const auto result = alice.files.start("bob", path); REQUIRE(result);
    for (std::size_t i = 0; i <= bytes / (4 * 65536); ++i) REQUIRE(alice.files.tick());
    REQUIRE(alice.chat.handleBundle(bob.bundle()));
    const auto sender = alice.store.load().value(); REQUIRE(bob.files.receiveMetadata(sender.outbox.front()));
    REQUIRE(bob.files.tick()); const auto receiver = bob.store.load().value(); REQUIRE(receiver.transfers.front().lastAck);
    REQUIRE(alice.files.receiveAck(*receiver.transfers.front().lastAck));
    return result.value();
}
}
TEST_CASE("M7-01 bounded window pipelines sixteen persisted chunks and rejects ACK beyond window") {
    test::FileClient alice("alice"), bob("bob"); const auto id = ready(alice, bob, 24 * 65536);
    alice.net.sent.clear();
    for (int i = 0; i < 4; ++i) REQUIRE(alice.files.tick());
    auto snapshot = alice.store.load().value(); const auto& pending = snapshot.transfers.front().pendingChunks;
    REQUIRE(pending.size() == 16); CHECK(alice.net.sent.size() == 16);
    REQUIRE(alice.files.tick()); CHECK(alice.net.sent.size() == 16); // Window is full until durable ACK.
    CHECK_FALSE(alice.files.acknowledge("bob", id, 16)); CHECK_FALSE(alice.files.acknowledge("mallory", id, 3));
    CHECK_FALSE(alice.files.acknowledge("bob", id, 3, 1));
    CHECK(alice.files.progress(id).value().authenticatedBytes == 0);
    for (int i : {3, 1, 2, 0}) REQUIRE(bob.files.receiveChunk(pending[i].envelope));
    const auto before = bob.store.load().value().revision; REQUIRE(bob.files.tick());
    const auto saved = bob.store.load().value(); CHECK(saved.transfers.front().nextContiguousChunk == 4);
    CHECK(saved.revision - before <= 4); // One batch checkpoint plus encrypted cumulative ACK persistence.
    REQUIRE(alice.files.receiveAck(*saved.transfers.front().lastAck));
    CHECK(alice.files.progress(id).value().authenticatedBytes == 4 * 65536);
    REQUIRE(alice.files.tick()); CHECK(alice.store.load().value().transfers.front().pendingChunks.size() == 16);
    cc::Bytes bytes; REQUIRE(cc::base64Decode(pending.front().envelope.payloadBase64, bytes)); CHECK(bytes.starts_with("CCF3"));
    CHECK(pending.front().envelope.payloadBase64.size() < 88000); // One Base64 layer, about 87.5 KB for 64 KiB.
}
TEST_CASE("M7-01 recreation resends every unacknowledged chunk byte-for-byte without advancing keys") {
    test::FileClient bob("bob"); const auto directory = test::tempDirectory("window-restart");
    ClientSnapshot saved; std::string id;
    {
        test::FileClient alice("alice", directory); id = ready(alice, bob, 20 * 65536);
        for (int i = 0; i < 4; ++i) REQUIRE(alice.files.tick());
        saved = alice.store.load().value(); REQUIRE(saved.transfers.front().pendingChunks.size() == 16);
    }
    test::FileClient reopened("alice", directory); REQUIRE(reopened.files.resume(id));
    for (int i = 0; i < 10; ++i) REQUIRE(reopened.files.tick());
    for (const auto& pending : saved.transfers.front().pendingChunks) {
        const auto found = std::find_if(reopened.net.sent.begin(), reopened.net.sent.end(), [&](const auto& m) { return m.msgId == pending.envelope.messageId; });
        REQUIRE(found != reopened.net.sent.end()); CHECK(cc::proto::bodyStr(found->body, "payload") == pending.envelope.payloadBase64);
    }
    CHECK(reopened.store.load().value().transfers.front().fileKey == saved.transfers.front().fileKey);
    CHECK(reopened.files.progress(id).value().authenticatedBytes == 0);
}
TEST_CASE("M7-01 legacy wire payloads and old active checkpoints cannot use a fallback") {
    test::FileClient alice("alice"), bob("bob"); alice.trust(bob); bob.trust(alice);
    auto packet = test::packets(alice, bob, "old-mode-is-removed");
    REQUIRE(bob.files.receiveMetadata(packet.metadata)); REQUIRE(bob.files.tick());
    auto legacy = packet.chunks.front();
    const nlohmann::json old = {{"version", 2}, {"file_id", packet.id}, {"seq", 0}, {"cipher_base64", "YQ=="}, {"tag_base64", "YQ=="}};
    legacy.payloadBase64 = cc::base64Encode(old.dump()); CHECK_FALSE(bob.files.receiveChunk(legacy));
    auto state = bob.store.load().value(); state.transfers.front().transportVersion = 2;
    const auto revision = state.revision++; REQUIRE(bob.store.commit(revision, state));
    CHECK_FALSE(bob.files.resume(packet.id)); CHECK(bob.files.progress(packet.id).value().authenticatedBytes == 0);
    CHECK(std::filesystem::exists(state.transfers.front().sourceOrPartialPath));
}
TEST_CASE("M7-01 flat CBOR rejects nested maps arrays duplicate keys and truncated bodies") {
    test::FileClient alice("alice"), bob("bob"); alice.trust(bob); bob.trust(alice);
    auto packet = test::packets(alice, bob, "strict-cbor");
    REQUIRE(bob.files.receiveMetadata(packet.metadata)); REQUIRE(bob.files.tick()); bob.net.sent.clear();
    const auto revision = bob.store.load().value().revision;
    auto rejected = [&](const cc::Bytes& body) {
        auto malformed = packet.chunks.front(); malformed.payloadBase64 = cc::base64Encode("CCF3" + body);
        CHECK_FALSE(bob.files.receiveChunk(malformed));
        CHECK(bob.store.load().value().revision == revision); CHECK(bob.net.sent.empty());
    };
    rejected(cc::Bytes(1000, static_cast<char>(0x81))); // Nested single-element arrays, rejected at the first event.
    const auto deepMap = nlohmann::json::to_cbor(nlohmann::json{{"version", 2}, {"file_id", packet.id}, {"seq", 0},
        {"cipher", nlohmann::json{{"nested", 1}}}, {"tag", codec::binary(cc::Bytes(16, 't'))}});
    rejected(cc::Bytes(deepMap.begin(), deepMap.end()));
    cc::Bytes duplicate(1, static_cast<char>(0xa5)); // Five identical map keys.
    for (int i = 0; i < 5; ++i) duplicate += cc::Bytes("\x67version\x02", 9);
    rejected(duplicate);
    cc::Bytes valid; REQUIRE(cc::base64Decode(packet.chunks.front().payloadBase64, valid));
    rejected(valid.substr(4, valid.size() - 5));
    rejected(valid.substr(4) + "trailing");
    REQUIRE(bob.files.receiveChunk(packet.chunks.front())); REQUIRE(bob.files.tick());
    CHECK(bob.files.progress(packet.id).value().complete);
}
TEST_CASE("M7-01 retired transfer queues stay readable without retransmission or using new slots") {
    test::FileClient alice("alice"), bob("bob"); const auto oldId = ready(alice, bob, 100);
    auto state = alice.store.load().value(); state.transfers.front().transportVersion = 2;
    state.pendingSends.push_back({"fmeta:" + oldId, "", "alice", "bob", "file_meta", "{\"version\":2}", 0, 1});
    state.pendingPayloads.push_back({"fack:" + oldId + ":0", "", "bob", "alice", "file_ack", "{\"version\":2}", 0, 1});
    while (state.transfers.size() < 16) {
        auto legacy = state.transfers.front(); legacy.fileId = wire::id(); state.transfers.push_back(std::move(legacy));
    }
    const auto revision = state.revision++; REQUIRE(alice.store.commit(revision, state));
    alice.net.sent.clear(); REQUIRE(alice.chat.flushOutbox()); REQUIRE(alice.chat.handleBundle(bob.bundle())); REQUIRE(alice.files.tick());
    CHECK(alice.net.sent.empty()); CHECK_FALSE(alice.files.resume(oldId));
    const auto source = alice.directory / "new-transfer.bin"; test::writeBytes(source, "new-file");
    const auto created = alice.files.start("bob", source); REQUIRE(created); REQUIRE(alice.files.tick());
    REQUIRE(alice.chat.handleBundle(bob.bundle()));
    bool sentNew = false;
    for (const auto& message : alice.net.sent) {
        CHECK(message.msgId != "fmeta:" + oldId);
        sentNew |= message.msgId == "fmeta:" + created.value();
    }
    CHECK(sentNew); CHECK(alice.store.load().value().transfers.size() == 17);
}
