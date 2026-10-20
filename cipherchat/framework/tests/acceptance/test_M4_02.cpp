#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
using namespace ccfw;

TEST_CASE("M8-04 file send time survives authenticated metadata resume and legacy snapshot decoding") {
    const auto aDirectory = test::tempDirectory("timeline-sender"), bDirectory = test::tempDirectory("timeline-receiver");
    std::string id; std::int64_t created = 0;
    {
        test::FileClient alice("alice", aDirectory), bob("bob", bDirectory); alice.trust(bob); bob.trust(alice);
        const auto source = aDirectory / "timestamp-source.bin"; test::writeBytes(source, cc::Bytes(5 * 65536, 't'));
        const auto before = wire::now(); const auto started = alice.files.start("bob", source); REQUIRE(started); id = started.value();
        created = alice.files.progress(id).value().timestamp; CHECK(created >= before); CHECK(created <= wire::now());
        REQUIRE(alice.files.tick()); REQUIRE(alice.files.tick()); REQUIRE(alice.chat.handleBundle(bob.bundle()));
        auto metadata = alice.store.load().value().outbox.front(); metadata.timestamp = created + 60000;
        REQUIRE(bob.files.receiveMetadata(metadata));
        CHECK(bob.files.progress(id).value().timestamp == created); // Uses the authenticated inner timestamp.
        REQUIRE(bob.files.tick()); CHECK(bob.files.progress(id).value().timestamp == created);
        auto checkpoint = alice.store.load().value().transfers.front();
        auto legacy = nlohmann::json(checkpoint); legacy.erase("timestamp");
        CHECK(legacy.get<TransferCheckpoint>().timestamp == 0);
        legacy["timestamp"] = -1; CHECK_THROWS(legacy.get<TransferCheckpoint>());
    }
    test::FileClient reopenedAlice("alice", aDirectory), reopenedBob("bob", bDirectory);
    CHECK(reopenedAlice.files.progress(id).value().timestamp == created);
    CHECK(reopenedBob.files.progress(id).value().timestamp == created);
    REQUIRE(reopenedAlice.files.resume(id)); REQUIRE(reopenedAlice.files.tick());
    CHECK(reopenedAlice.files.progress(id).value().timestamp == created);
}

TEST_CASE("M4-02 real TLS files including empty and short tails have exact bytes and SHA256") {
    test::RealServer server; test::RealClient alice("alice"), bob("bob");
    alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options()); alice.trust(bob); bob.trust(alice);
    for (std::size_t size : {0, 1, 15, 16, 65535, 65536, 65537}) {
        CAPTURE(size); const auto bytes = cc::crypto::randomBytes(size); const auto source = alice.directory / ("source-" + std::to_string(size) + ".bin");
        test::writeBytes(source, bytes); auto started = alice.files.start("bob", source); REQUIRE(started);
        REQUIRE(test::eventually([&] {
            alice.application.tick(); bob.application.tick();
            auto a = alice.files.progress(started.value()), b = bob.files.progress(started.value());
            return a && b && a.value().complete && b.value().complete;
        }, 6000));
        const auto result = bob.files.progress(started.value()).value(); CHECK(result.authenticatedBytes == size);
        const auto actual = test::readBytes(result.path); CHECK(actual == bytes);
        CHECK(cc::crypto::sha256(actual) == cc::crypto::sha256(bytes));
        auto partial = result.path; partial += ".part"; CHECK_FALSE(std::filesystem::exists(partial));
    }
    CHECK(alice.events.ofType<Error>().empty()); CHECK(bob.events.ofType<Error>().empty());
}
TEST_CASE("M4-02 file primitive accepts zero to fifteen byte cipher and rejects wrong tags") {
    const auto key = cc::crypto::randomBytes(32), id = cc::crypto::randomBytes(16);
    for (std::size_t size = 0; size < 16; ++size) {
        const auto bytes = cc::crypto::randomBytes(size); auto encrypted = cc::files::sealChunk(key, id, 0, "", bytes);
        auto decoded = cc::files::openChunk(key, id, 0, "", encrypted.cipher, encrypted.tag); REQUIRE(decoded); CHECK(*decoded == bytes);
        encrypted.tag[0] ^= 1; CHECK_FALSE(cc::files::openChunk(key, id, 0, "", encrypted.cipher, encrypted.tag));
        CHECK_FALSE(cc::files::openChunk(key, id, 0, "", encrypted.cipher, "short"));
    }
}
TEST_CASE("M4-02 window reordering tampering and duplicate chunks do not inflate durable progress") {
    test::FileClient alice("alice"), bob("bob"); alice.trust(bob); bob.trust(alice);
    const auto data = cc::crypto::randomBytes(4 * 65536 + 1); auto packets = test::packets(alice, bob, data, "../../CON.bin");
    REQUIRE(bob.files.receiveMetadata(packets.metadata)); REQUIRE(bob.files.tick());
    auto checkpoint = bob.store.load().value().transfers.front();
    CHECK(checkpoint.finalPath.parent_path() == std::filesystem::canonical(bob.directory / "downloads"));
    CHECK_FALSE(std::filesystem::exists(checkpoint.finalPath));
    for (int i : {3, 2, 1}) REQUIRE(bob.files.receiveChunk(packets.chunks[i]));
    REQUIRE(bob.files.tick()); CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 0);
    auto invalid = packets.chunks[0]; bool cipherError = false;
    SUBCASE("ciphertext") {
        cipherError = true; cc::Bytes raw; REQUIRE(cc::base64Decode(invalid.payloadBase64, raw));
        auto body = nlohmann::json::from_cbor(raw.begin() + 4, raw.end()); auto cipher = codec::bytes(body.at("cipher"));
        cipher[0] ^= 1; body["cipher"] = codec::binary(cipher);
        auto cbor = nlohmann::json::to_cbor(body); raw = "CCF3"; raw.append(reinterpret_cast<const char*>(cbor.data()), cbor.size());
        invalid.payloadBase64 = cc::base64Encode(raw);
    }
    SUBCASE("wrong peer") { invalid.from = "mallory"; }
    SUBCASE("wrong destination") { invalid.to = "mallory"; }
    if (cipherError) { REQUIRE(bob.files.receiveChunk(invalid)); REQUIRE_FALSE(bob.files.tick()); }
    else REQUIRE_FALSE(bob.files.receiveChunk(invalid));
    CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 0);
    REQUIRE(bob.files.receiveChunk(packets.chunks[0])); REQUIRE(bob.files.tick());
    CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 4 * 65536);
    REQUIRE(bob.files.receiveChunk(packets.chunks[0])); CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 4 * 65536);
    REQUIRE(bob.files.receiveChunk(packets.chunks[4])); REQUIRE(bob.files.tick());
    auto finished = bob.files.progress(packets.id).value(); REQUIRE(finished.complete);
    CHECK(test::readBytes(finished.path) == data); CHECK_FALSE(bob.files.receiveChunk(invalid));
}
TEST_CASE("M4-02 receiver batch flush precedes commit and a failed checkpoint has no ACK") {
    test::FileClient alice("alice"), bob("bob"); alice.trust(bob); bob.trust(alice);
    auto packets = test::packets(alice, bob, cc::crypto::randomBytes(4 * 65536 + 1));
    REQUIRE(bob.files.receiveMetadata(packets.metadata)); REQUIRE(bob.files.tick()); bob.net.sent.clear();
    const auto partial = bob.store.load().value().transfers.front().sourceOrPartialPath;
    for (int i = 0; i < 4; ++i) REQUIRE(bob.files.receiveChunk(packets.chunks[i]));
    bob.store.fail = true; REQUIRE_FALSE(bob.files.tick());
    CHECK(std::filesystem::file_size(partial) == 4 * 65536); CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 0);
    CHECK(bob.net.sent.empty()); bob.store.fail = false;
    REQUIRE(bob.files.resume(packets.id)); CHECK(std::filesystem::file_size(partial) == 0); REQUIRE(bob.files.tick());
    CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 4 * 65536);
}
TEST_CASE("M4-02 receiver reordering budget is bounded to sixty-four blocks") {
    test::FileClient alice("alice"), bob("bob"); alice.trust(bob); bob.trust(alice);
    auto packets = test::packets(alice, bob, cc::Bytes(67 * 65536, 'x'));
    REQUIRE(bob.files.receiveMetadata(packets.metadata)); REQUIRE(bob.files.tick());
    for (int i = 0; i < 64; ++i) REQUIRE(bob.files.receiveChunk(packets.chunks[i]));
    auto overflow = bob.files.receiveChunk(packets.chunks[64]); REQUIRE_FALSE(overflow); CHECK(overflow.error().code == ErrorCode::Forbidden);
    CHECK(bob.files.progress(packets.id).value().authenticatedBytes == 0);
}
TEST_CASE("M4-02 streaming preparation and authenticated acknowledgements are bounded") {
    test::FileClient alice("alice"), bob("bob"); alice.trust(bob); bob.trust(alice);
    const auto path = alice.directory / "large.bin"; test::writeBytes(path, cc::Bytes(5 * 65536, 's'));
    const auto started = alice.files.start("bob", path); REQUIRE(started); REQUIRE(alice.files.tick());
    CHECK_FALSE(alice.store.load().value().transfers.front().metadataQueued); REQUIRE(alice.files.tick());
    CHECK(alice.store.load().value().transfers.front().metadataQueued);
    REQUIRE(alice.chat.handleBundle(bob.bundle()));
    REQUIRE(bob.files.receiveMetadata(alice.store.load().value().outbox.front())); REQUIRE(bob.files.tick());
    const auto ack = bob.store.load().value().transfers.front().lastAck; REQUIRE(ack);
    auto invalid = *ack; invalid.from = "mallory"; CHECK_FALSE(alice.files.receiveAck(invalid));
    CHECK_FALSE(alice.files.acknowledge("mallory", started.value(), UINT32_MAX));
    REQUIRE(alice.files.receiveAck(*ack)); REQUIRE(alice.files.tick());
    CHECK(alice.store.load().value().transfers.front().pendingChunks.size() == 4);
    CHECK_FALSE(alice.files.acknowledge("bob", started.value(), 4));
    CHECK_FALSE(alice.files.acknowledge("bob", "unknown", 0));
    CHECK(alice.files.progress(started.value()).value().authenticatedBytes == 0);
    REQUIRE(alice.files.cancel(started.value())); CHECK_FALSE(alice.files.resume(started.value()));
    CHECK(alice.files.progress(started.value()).value().cancelled); CHECK(std::filesystem::exists(path));
}
