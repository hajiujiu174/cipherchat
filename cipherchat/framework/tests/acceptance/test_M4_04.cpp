#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include "process_fixture.hpp"
#include <cstdlib>
using namespace ccfw;
TEST_CASE("M4-04 two native GUI processes register verify identities chat transfer files and create group") {
    const bool layoutChecks = std::getenv("CCFW_GUI_LAYOUT_CHECKS") != nullptr;
    test::RealServer server; const auto directory = test::tempDirectory("M4-04-gui");
    const auto source = directory / codec::path("校园网通信与文件传输架构设计方案-聊天文件卡片测试.bin"); const auto data = cc::crypto::randomBytes(1024 * 1024 + 15); test::writeBytes(source, data);
    auto config = [&](bool sending) {
        return nlohmann::json{{"sending", sending}, {"layout_checks", layoutChecks}, {"username", sending ? "gui_alice" : "gui_bob"}, {"peer", sending ? "gui_bob" : "gui_alice"},
            {"port", server.options().port}, {"ca", codec::pathText(server.options().caFile)}, {"data", codec::pathText(directory / (sending ? "alice-data" : "bob-data"))},
            {"artifacts", codec::pathText(directory / (sending ? "alice-screens" : "bob-screens"))}, {"source", codec::pathText(source)},
            {"own_identity", codec::pathText(directory / (sending ? "alice.identity" : "bob.identity"))},
            {"peer_identity", codec::pathText(directory / (sending ? "bob.identity" : "alice.identity"))},
            {"own_message", sending ? "你好，GUI 双窗口加密聊天测试。" : "已收到，身份核对与消息签收正常。"},
            {"peer_message", sending ? "已收到，身份核对与消息签收正常。" : "你好，GUI 双窗口加密聊天测试。"}};
    };
    const auto aConfig = directory / "alice.json", bConfig = directory / "bob.json";
    test::writeConfig(aConfig, config(true)); test::writeConfig(bConfig, config(false));
    test::Process alice(aConfig, true), bob(bConfig, true);
    const bool completed = test::eventually([&] {
        return (std::filesystem::exists(aConfig.string() + ".done") && std::filesystem::exists(bConfig.string() + ".done")) || !alice.running() || !bob.running();
    }, 50000);
    INFO(directory.string());
    INFO(alice.exitCode()); INFO(bob.exitCode());
    INFO((std::filesystem::exists(aConfig.string() + ".error") ? test::readBytes(aConfig.string() + ".error") : "Alice no error"));
    INFO((std::filesystem::exists(bConfig.string() + ".error") ? test::readBytes(bConfig.string() + ".error") : "Bob no error"));
    REQUIRE(completed); REQUIRE(std::filesystem::exists(aConfig.string() + ".done")); REQUIRE(std::filesystem::exists(bConfig.string() + ".done"));
    for (const auto& file : {aConfig, bConfig}) {
        auto evidence = nlohmann::json::parse(test::readBytes(file.string() + ".done"));
        CHECK(evidence["layout_checks"].get<bool>() == layoutChecks);
        CHECK(evidence["security_details_verified"].get<bool>());
        if (layoutChecks) {
            CHECK(evidence["default_connection_hidden"].get<bool>());
            CHECK(evidence["files_collapsed_by_default"].get<bool>()); CHECK(evidence["completed_while_collapsed"].get<bool>());
        }
        CHECK(evidence["conversation_filter_verified"].get<bool>()); CHECK(evidence["file_timestamp_stable"].get<bool>()); CHECK(evidence["timeline_order_verified"].get<bool>());
        CHECK(evidence["identity_confirmed"].get<bool>()); CHECK(evidence["progress_seen"].get<bool>());
        CHECK(evidence["gui_heartbeats"].get<int>() > 20); CHECK(evidence["max_gui_gap_ms"].get<int>() < 1500); CHECK(evidence["native_window"].get<std::uint64_t>() != 0);
        std::ofstream stop(file.string() + ".stop"); stop << "verified";
    }
    REQUIRE(test::eventually([&] { return !alice.running() && !bob.running(); }, 6000)); CHECK(alice.exitCode() == 0); CHECK(bob.exitCode() == 0);
    const auto restart = directory / "relogin"; std::filesystem::create_directories(restart);
    auto restartConfig = [&](bool sending) {
        auto value = config(sending); const auto role = sending ? "alice" : "bob";
        const auto first = nlohmann::json::parse(test::readBytes((sending ? aConfig : bConfig).string() + ".done"));
        value["register_new"] = false; value["chat_only"] = true; value["verify_timeline_on_login"] = true;
        value["expected_file_timestamp"] = first.at("file_timestamp");
        value["expected_own_identity"] = codec::pathText(directory / (std::string(role) + ".identity"));
        value["artifacts"] = codec::pathText(restart / role);
        value["own_identity"] = codec::pathText(restart / (std::string(role) + ".identity"));
        value["peer_identity"] = codec::pathText(restart / (sending ? "bob.identity" : "alice.identity"));
        return value;
    };
    const auto restartA = restart / "alice.json", restartB = restart / "bob.json";
    test::writeConfig(restartA, restartConfig(true)); test::writeConfig(restartB, restartConfig(false));
    test::Process aliceAgain(restartA, true), bobAgain(restartB, true);
    REQUIRE(test::eventually([&] { return (std::filesystem::exists(restartA.string() + ".done") && std::filesystem::exists(restartB.string() + ".done")) || !aliceAgain.running() || !bobAgain.running(); }, 20000));
    for (const auto& file : {restartA, restartB}) {
        INFO((std::filesystem::exists(file.string() + ".error") ? test::readBytes(file.string() + ".error") : "No restart error"));
        REQUIRE(std::filesystem::exists(file.string() + ".done"));
        const auto result = nlohmann::json::parse(test::readBytes(file.string() + ".done"));
        CHECK(result.at("timeline_order_verified") == true); CHECK(result.at("file_timestamp_stable") == true); CHECK(result.at("existing_identity_matched") == true);
        std::ofstream stop(file.string() + ".stop"); stop << "verified";
    }
    REQUIRE(test::eventually([&] { return !aliceAgain.running() && !bobAgain.running(); }, 6000)); CHECK(aliceAgain.exitCode() == 0); CHECK(bobAgain.exitCode() == 0);
    const auto bProfiles = directory / "bob-data"; std::filesystem::path bDb;
    for (const auto& profile : std::filesystem::directory_iterator(bProfiles)) if (profile.is_directory()) bDb = profile.path() / "client.db";
    REQUIRE_FALSE(bDb.empty()); PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(bDb, {"127.0.0.1:" + std::to_string(server.options().port) + "/localhost", "gui_bob"}));
    const auto snapshot = store.load(); REQUIRE(snapshot); REQUIRE(snapshot.value().transfers.size() == 1);
    CHECK(snapshot.value().transfers.front().complete); CHECK(test::readBytes(snapshot.value().transfers.front().finalPath) == data);
    REQUIRE(snapshot.value().groups.size() == 1); CHECK(snapshot.value().history.size() == 4);
}
