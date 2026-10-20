#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include "process_fixture.hpp"
using namespace ccfw;
TEST_CASE("M5-03 offline chat survives sender crash receiver crash lost ACK and exact ciphertext replay") {
    test::RealServer server; const auto directory = test::tempDirectory("M5-03-process");
    std::filesystem::path aPath, bPath;
    {
        test::RealClient alice("alice"), bob("bob"); aPath = alice.directory; bPath = bob.directory;
        alice.registerAndLogin(server.options()); bob.registerAndLogin(server.options()); alice.trust(bob); bob.trust(alice);
    }
    const std::string message = "cross-process-private-message-938fd20";
    auto config = [&](bool sending, const std::string& phase) {
        return nlohmann::json{{"operation", "chat"}, {"text", message}, {"directory", codec::pathText(sending ? aPath : bPath)},
            {"username", sending ? "alice" : "bob"}, {"port", server.options().port}, {"ca", codec::pathText(server.options().caFile)},
            {"sending", sending}, {"peer", sending ? "bob" : "alice"}, {"pause_chat", phase == "before"},
            {"drop_receipts", !sending && phase == "before"}, {"audit", codec::pathText(directory / (phase + (sending ? "-a.jsonl" : "-b.jsonl")))}};
    };
    const auto firstA = directory / "before-a.json", firstB = directory / "before-b.json";
    test::writeConfig(firstA, config(true, "before")); test::writeConfig(firstB, config(false, "before"));
    {
        test::Process sender(firstA);
        REQUIRE(test::eventually([&] { return std::filesystem::exists(firstA.string() + ".paused"); }, 8000)); sender.crash(); CHECK(sender.exitCode() == 77);
    }
    const auto beforeA = test::loadDisk(aPath, "alice"); REQUIRE(beforeA.outbox.size() == 1);
    const auto saved = beforeA.outbox.front(); CHECK(beforeA.history.front().state == DeliveryState::ServerAccepted);
    {
        test::Process receiver(firstB);
        REQUIRE(test::eventually([&] { return std::filesystem::exists(firstB.string() + ".paused"); }, 8000)); receiver.crash(); CHECK(receiver.exitCode() == 77);
    }
    const auto beforeB = test::loadDisk(bPath, "bob"); REQUIRE(beforeB.history.size() == 1); CHECK(beforeB.history.front().text == message);
    const auto resumeA = directory / "resume-a.json", resumeB = directory / "resume-b.json";
    test::writeConfig(resumeA, config(true, "resume")); test::writeConfig(resumeB, config(false, "resume"));
    {
        test::Process sender(resumeA), receiver(resumeB);
        REQUIRE(test::eventually([&] { return !sender.running() && std::filesystem::exists(resumeB.string() + ".done"); }, 10000));
        INFO((std::filesystem::exists(resumeA.string() + ".error") ? test::readBytes(resumeA.string() + ".error") : "no sender error"));
        REQUIRE(sender.exitCode() == 0);
        { std::ofstream stop(resumeB.string() + ".stop"); stop << "done"; }
        REQUIRE(test::eventually([&] { return !receiver.running(); }, 5000)); REQUIRE(receiver.exitCode() == 0);
    }
    const auto afterA = test::loadDisk(aPath, "alice"), afterB = test::loadDisk(bPath, "bob");
    CHECK(afterA.outbox.empty()); REQUIRE(afterA.history.size() == 1); CHECK(afterA.history.front().state == DeliveryState::RecipientReceived);
    CHECK(afterB.history.size() == 1); CHECK(afterA.identity->signing.priv == beforeA.identity->signing.priv);
    const bool sameRatchet = nlohmann::json(afterB.sessions) == nlohmann::json(beforeB.sessions); CHECK(sameRatchet);
    std::ifstream audit(directory / "resume-a.jsonl"); std::string line; bool replayed = false;
    while (std::getline(audit, line)) {
        auto packet = cc::proto::deserialize(line); REQUIRE(packet);
        if (packet->msgId == saved.messageId) { CHECK(cc::proto::bodyStr(packet->body, "payload") == saved.payloadBase64); replayed = true; }
    }
    CHECK(replayed);
}
