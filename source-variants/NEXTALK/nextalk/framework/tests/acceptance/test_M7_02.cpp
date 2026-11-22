#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include "process_fixture.hpp"
using namespace ccfw;

TEST_CASE("M7-02 endpoint migration keeps the original GUI account identity and session") {
    test::RealServer server; const auto directory = test::tempDirectory("M7-02-migration");
    const auto scope = "127.0.0.1:" + std::to_string(server.options().port) + "/localhost";
    for (int round = 0; round < 2; ++round) {
        const auto stage = directory / (round ? "new-endpoint" : "original"); std::filesystem::create_directories(stage);
        auto config = [&](bool sender) {
            const auto role = sender ? "alice" : "bob";
            nlohmann::json result = {{"sending", sender}, {"username", sender ? "migration_a" : "migration_b"},
                {"peer", sender ? "migration_b" : "migration_a"}, {"host", round ? "127.0.0.2" : "127.0.0.1"},
                {"port", server.options().port}, {"server_name", "localhost"}, {"ca", codec::pathText(server.options().caFile)},
                {"data", codec::pathText(directory / (std::string(role) + "-data"))}, {"artifacts", codec::pathText(stage / role)},
                {"source", "unused"}, {"chat_only", true}, {"register_new", round == 0},
                {"own_identity", codec::pathText(stage / (std::string(role) + ".identity"))},
                {"peer_identity", codec::pathText(stage / (sender ? "bob.identity" : "alice.identity"))},
                {"own_message", std::string(sender ? "Alice migration " : "Bob migration ") + std::to_string(round)},
                {"peer_message", std::string(sender ? "Bob migration " : "Alice migration ") + std::to_string(round)}};
            if (round) {
                result["account_scope"] = scope;
                result["expected_own_identity"] = codec::pathText(directory / "original" / (std::string(role) + ".identity"));
            }
            return result;
        };
        const auto a = stage / "alice.json", b = stage / "bob.json";
        test::writeConfig(a, config(true)); test::writeConfig(b, config(false));
        test::Process alice(a, true), bob(b, true);
        REQUIRE(test::eventually([&] { return (std::filesystem::exists(a.string()+".done") && std::filesystem::exists(b.string()+".done")) || !alice.running() || !bob.running(); }, 20000));
        INFO(stage.string());
        for (const auto& path : {a,b}) {
            INFO((std::filesystem::exists(path.string()+".error") ? test::readBytes(path.string()+".error") : "no driver error"));
            REQUIRE(std::filesystem::exists(path.string()+".done"));
            const auto result = nlohmann::json::parse(test::readBytes(path.string()+".done"));
            CHECK(result.at("stage") == 6);
            if (round) { CHECK(result.at("existing_identity_matched") == true); CHECK(result.at("identity_confirmed") == false); }
            else CHECK(result.at("identity_confirmed") == true);
            std::ofstream stop(path.string()+".stop"); stop << "verified";
        }
        REQUIRE(test::eventually([&] { return !alice.running() && !bob.running(); }, 6000));
        CHECK(alice.exitCode() == 0); CHECK(bob.exitCode() == 0);
    }
    for (const auto* role : {"alice","bob"}) {
        std::size_t profiles = 0;
        for (const auto& item : std::filesystem::directory_iterator(directory / (std::string(role)+"-data"))) if (item.is_directory()) ++profiles;
        CHECK(profiles == 1);
    }
}
