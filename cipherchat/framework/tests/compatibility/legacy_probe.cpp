// Acceptance-only executable linked exclusively with the frozen pre-M10 core.
// The crypto algorithms remain the original common/ implementation.
#include "ccfw/application.hpp"
#include "snapshot_codec.hpp"
#include <fstream>
#include <thread>
#include <iostream>
using namespace ccfw;
namespace {
struct Events : IEventSink { void publish(ClientEvent) override {} };
void require(const Result<>& r) { if (!r) throw std::runtime_error(r.error().message); }
nlohmann::json read(const std::filesystem::path& p) { try { std::ifstream f(p); nlohmann::json j; f >> j; return j; } catch (...) { return nlohmann::json::object(); } }
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto configPath = codec::path(argv[1]);
    try {
        const auto config = read(configPath); const auto directory = codec::path(config.at("directory").get<std::string>());
        const auto name = config.at("username").get<std::string>();
        PlatformSecretProtector protector; SqliteLocalStore store(protector); require(store.open(directory / "client.db", {"real-test-server", name}));
        TlsTransport net; SessionService sessions(store); AccountService account(net, store); ChatService chat(net, sessions, store);
        FileTransferService files(net, store, sessions); GroupService groups(chat, net, {"real-test-server", name}, &store); Events events;
        ClientApplication app(net, account, chat, files, groups, events); require(files.setDownloadDirectory(directory / "downloads"));
        require(app.connect({"127.0.0.1", config.at("port").get<std::uint16_t>(), codec::path(config.at("ca").get<std::string>()), "localhost", 3000}));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40); bool logged = false; std::string last;
        while (!std::filesystem::exists(configPath.string() + ".stop")) {
            (void)app.tick(); if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("legacy acceptance timed out");
            if (!logged && account.phase() == AccountPhase::Connected) { require(account.login(name, "password123")); logged = true; }
            if (account.phase() == AccountPhase::Online) {
                auto request = read(configPath.string() + ".request");
                if (request.contains("id") && request["id"] != last) {
                    last = request["id"].get<std::string>();
                    if (request.at("command") == "chat") { auto r = chat.sendText({config.at("peer").get<std::string>(), request.at("text").get<std::string>(), 0, ""}); if (!r) throw std::runtime_error(r.error().message); }
                    if (request.at("command") == "file") { auto r = files.start(config.at("peer").get<std::string>(), codec::path(request.at("path").get<std::string>())); if (!r) throw std::runtime_error(r.error().message); }
                }
                const auto state = store.load(); if (!state) throw std::runtime_error(state.error().message);
                std::ofstream(configPath.string() + ".status") << nlohmann::json{{"online", true}, {"history", state.value().history}, {"transfers", state.value().transfers}}.dump();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        app.shutdown(); return 0;
    } catch (const std::exception& e) { std::ofstream(configPath.string() + ".error") << e.what(); return 1; }
}
