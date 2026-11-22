#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include "process_fixture.hpp"
using namespace ccfw;

TEST_CASE("M4-03 terminate both TLS client processes and resume persisted ciphertext and partial file") {
    test::RealServer server({},false); // Exercise the original relay checkpoints; cache upload has its own cursor/ACK tests.
    const auto directory = test::tempDirectory("M4-03-process");
    std::filesystem::path aPath, bPath;
    {
        test::RealClient alice("alice"), bob("bob"); aPath = alice.directory; bPath = bob.directory;
        alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options()); alice.trust(bob); bob.trust(alice);
    }
    const auto source = aPath / "process-source.bin"; const auto data = cc::crypto::randomBytes(2 * 1024 * 1024 + 1); test::writeBytes(source, data);
    auto config = [&](bool sending, const std::string& phase) {
        return nlohmann::json{{"directory", codec::pathText(sending ? aPath : bPath)}, {"username", sending ? "alice" : "bob"},
            {"port", server.options().port}, {"ca", codec::pathText(server.options().caFile)}, {"sending", sending},
            {"peer", sending ? "bob" : "alice"}, {"source", codec::pathText(source)},
            {"audit", codec::pathText(directory / (phase + (sending ? "-a-wire.jsonl" : "-b-wire.jsonl")))}, {"pause_at", phase == "before" ? 131072 : 0}};
    };
    const auto firstA = directory / "before-a.json", firstB = directory / "before-b.json";
    test::writeConfig(firstA, config(true, "before")); test::writeConfig(firstB, config(false, "before"));
    {
        test::Process receiver(firstB), sender(firstA);
        REQUIRE(test::eventually([&] {
            return std::filesystem::exists(firstA.string() + ".paused") && std::filesystem::exists(firstB.string() + ".paused");
        }, 12000));
        sender.crash(); receiver.crash(); CHECK(sender.exitCode() == 77); CHECK(receiver.exitCode() == 77);
    }
    const auto beforeA = test::loadDisk(aPath, "alice"), beforeB = test::loadDisk(bPath, "bob");
    REQUIRE(beforeA.transfers.size() == 1); REQUIRE(beforeB.transfers.size() == 1);
    CHECK(beforeA.transfers.front().nextContiguousChunk == 4); CHECK(beforeB.transfers.front().nextContiguousChunk == 4);
    const auto partial = beforeB.transfers.front().sourceOrPartialPath;
    { std::ofstream extra(partial, std::ios::binary | std::ios::app); extra << "uncommitted-tail"; }
    CHECK_FALSE(std::filesystem::exists(beforeB.transfers.front().finalPath));
    const auto resumeA = directory / "resume-a.json", resumeB = directory / "resume-b.json";
    test::writeConfig(resumeA, config(true, "resume")); test::writeConfig(resumeB, config(false, "resume"));
    {
        test::Process receiver(resumeB), sender(resumeA);
        const bool receiverSettled = test::eventually([&] { return !receiver.running() && std::filesystem::exists(resumeA.string() + ".done"); }, 25000);
        INFO((std::filesystem::exists(resumeA.string() + ".error") ? test::readBytes(resumeA.string() + ".error") : "sender no error"));
        INFO((std::filesystem::exists(resumeB.string() + ".error") ? test::readBytes(resumeB.string() + ".error") : "receiver no error"));
        REQUIRE(receiverSettled);
        { std::ofstream stop(resumeA.string() + ".stop"); stop << "done"; }
        REQUIRE(test::eventually([&] { return !sender.running(); }, 5000));
        REQUIRE(sender.exitCode() == 0); REQUIRE(receiver.exitCode() == 0);
    }
    const auto afterA = test::loadDisk(aPath, "alice"), afterB = test::loadDisk(bPath, "bob");
    REQUIRE(afterA.transfers.front().complete); REQUIRE(afterB.transfers.front().complete);
    CHECK(test::readBytes(afterB.transfers.front().finalPath) == data); CHECK_FALSE(std::filesystem::exists(partial));
    CHECK(afterA.transfers.front().fileKey == beforeA.transfers.front().fileKey);
    REQUIRE_FALSE(beforeA.transfers.front().pendingChunks.empty());
    for (const auto& pending : beforeA.transfers.front().pendingChunks) {
        const auto& saved = pending.envelope;
        bool same = false;
        // A relay may deliver a pre-crash upload while the sender rechecks its source.
        // Both the original upload and any replay must use the persisted bytes.
        for (const auto* phase : {"before-a-wire.jsonl", "resume-a-wire.jsonl"}) {
            std::ifstream audit(directory / phase); std::string line;
            while (std::getline(audit, line)) { auto message = cc::proto::deserialize(line); REQUIRE(message);
                if (message->msgId == saved.messageId) { CHECK(cc::proto::bodyStr(message->body, "payload") == saved.payloadBase64); same = true; }
            }
        }
        CHECK(same);
    }
}
TEST_CASE("M4-03 same-size changed source refuses old key and changed partial prefix is rejected") {
    test::FileClient bob("bob"); const auto aPath = test::tempDirectory("source-resume"); std::string id; const auto source = aPath / "source.bin";
    test::writeBytes(source, cc::Bytes(4 * 65536 + 1, 'a'));
    {
        test::FileClient alice("alice", aPath); alice.trust(bob); bob.trust(alice);
        auto started = alice.files.start("bob", source); REQUIRE(started); id = started.value(); REQUIRE(alice.files.tick()); REQUIRE(alice.files.tick());
        REQUIRE(alice.chat.handleBundle(bob.bundle())); REQUIRE(bob.files.receiveMetadata(alice.store.load().value().outbox.front()));
        REQUIRE(bob.files.tick()); REQUIRE(alice.files.receiveAck(*bob.store.load().value().transfers.front().lastAck));
        REQUIRE(alice.files.tick());
        const auto prepared = alice.store.load().value();
        for (const auto& pending : prepared.transfers.front().pendingChunks) REQUIRE(bob.files.receiveChunk(pending.envelope));
        REQUIRE(bob.files.tick());
    }
    test::writeBytes(source, cc::Bytes(4 * 65536 + 1, 'b'));
    test::FileClient restored("alice", aPath); REQUIRE(restored.files.resume(id));
    REQUIRE(restored.files.tick()); auto rejected = restored.files.tick(); REQUIRE_FALSE(rejected); CHECK(rejected.error().code == ErrorCode::Conflict);
    CHECK(restored.net.sent.empty()); CHECK(restored.files.progress(id).value().authenticatedBytes == 0);
    const auto partial = bob.store.load().value().transfers.front().sourceOrPartialPath;
    test::writeBytes(partial, cc::Bytes(4 * 65536, 'x')); REQUIRE(bob.files.resume(id));
    auto altered = bob.files.tick(); REQUIRE_FALSE(altered); CHECK(altered.error().code == ErrorCode::AuthenticationFailed);
}
