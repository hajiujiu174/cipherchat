// Real subprocess used by recovery acceptance tests. No server or GUI code is linked.
#include "ccfw/application.hpp"
#include "../../src/snapshot_codec.hpp"
#include <fstream>
#include <thread>
#include <iostream>

namespace {
using namespace ccfw;
struct Events final : IEventSink { void publish(ClientEvent) override {} };
struct AuditTransport final : ITransport {
    TlsTransport inner;
    std::filesystem::path audit;
    bool dropReceipts = false;
    int receivedChats = 0;
    Result<> connect(const ConnectionOptions& o) override { return inner.connect(o); }
    Result<> send(const cc::proto::Message& m) override {
        if (dropReceipts && m.type == cc::proto::MsgType::RecipientAck) return Result<>::success({});
        auto result = inner.send(m);
        if (result && m.type == cc::proto::MsgType::SendMsg && !audit.empty()) {
            std::ofstream output(audit, std::ios::app | std::ios::binary); output << cc::proto::serialize(m) << '\n';
        }
        return result;
    }
    Result<std::optional<cc::proto::Message>> poll() override {
        auto result = inner.poll();
        if (result && result.value() && result.value()->type == cc::proto::MsgType::RecvMsg && cc::proto::bodyStr(result.value()->body, "kind") == "chat") ++receivedChats;
        return result;
    }
    void close() noexcept override { inner.close(); }
};
void check(const Result<>& result) { if (!result) throw std::runtime_error(result.error().message); }
template<class Predicate> void until(Predicate predicate, int seconds = 15) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) throw std::runtime_error("process milestone timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const auto configPath = ccfw::codec::path(argv[1]);
        std::ifstream input(configPath); nlohmann::json config; input >> config;
        const auto directory = codec::path(config.at("directory").get<std::string>());
        const auto name = config.at("username").get<std::string>();
        PlatformSecretProtector protector; SqliteLocalStore store(protector); AuditTransport net;
        net.audit = codec::path(config.at("audit").get<std::string>());
        net.dropReceipts = config.value("drop_receipts", false);
        check(store.open(directory / "client.db", {"real-test-server", name}));
        SessionService sessions(store); AccountService account(net, store); ChatService chat(net, sessions, store);
        FileTransferService files(net, store, sessions); GroupService groups(chat, net, {"real-test-server", name}, &store);
        Events events; ClientApplication app(net, account, chat, files, groups, events);
        check(files.setDownloadDirectory(directory / "downloads"));
        ConnectionOptions options{"127.0.0.1", config.at("port").get<std::uint16_t>(), codec::path(config.at("ca").get<std::string>()), "localhost", 3000};
        check(app.connect(options)); until([&] { check(app.tick()); return account.phase() == AccountPhase::Connected; });
        check(account.login(name, "password123")); until([&] { check(app.tick()); return account.phase() == AccountPhase::Online; });
        const bool sending = config.at("sending").get<bool>();
        if (config.value("operation", "file") == "chat") {
            const auto text = config.at("text").get<std::string>();
            const auto existing = store.load().value().history;
            if (sending && std::none_of(existing.begin(), existing.end(), [&](const auto& h) { return h.from == name && h.text == text; })) {
                auto sent = chat.sendText({config.at("peer").get<std::string>(), text, 0, ""});
                if (!sent) throw std::runtime_error(sent.error().message);
            }
            bool paused = false, announced = false;
            until([&] {
                if (paused) return false;
                check(app.tick()); const auto history = store.load().value().history;
                const auto found = std::find_if(history.begin(), history.end(), [&](const auto& h) { return h.text == text && (sending ? h.from == name : h.to == name); });
                if (found == history.end()) return false;
                if (config.value("pause_chat", false) && (!sending || found->state == DeliveryState::ServerAccepted)) {
                    std::ofstream flag(configPath.string() + ".paused"); flag << "persisted\n"; paused = true; return false;
                }
                if (sending ? found->state == DeliveryState::RecipientReceived : net.receivedChats > 0) {
                    if (!announced) {
                        std::ofstream done(configPath.string() + ".done");
                        done << nlohmann::json{{"message_id", found->messageId}, {"received_frames", net.receivedChats}, {"history_count", history.size()}}.dump(); announced = true;
                    }
                    return sending || std::filesystem::exists(configPath.string() + ".stop");
                }
                return false;
            }, 30);
            app.shutdown(); return 0;
        }
        if (sending && store.load().value().transfers.empty()) {
            auto start = files.start(config.at("peer").get<std::string>(), codec::path(config.at("source").get<std::string>()));
            if (!start) throw std::runtime_error(start.error().message);
        }
        const auto pauseAt = config.value("pause_at", std::uint64_t{0});
        bool paused = false;
        until([&] {
            if (paused) return false; // Parent forcibly terminates this real process at the persisted milestone.
            check(app.tick());
            const auto snapshot = store.load(); if (!snapshot) throw std::runtime_error(snapshot.error().message);
            if (snapshot.value().transfers.empty()) return false;
            const auto id = snapshot.value().transfers.front().fileId;
            const auto progress = files.progress(id); if (!progress) throw std::runtime_error(progress.error().message);
            if (pauseAt && progress.value().authenticatedBytes >= pauseAt) {
                std::ofstream flag(configPath.string() + ".paused"); flag << "persisted\n"; flag.close(); paused = true;
                return false;
            }
            if (progress.value().complete && snapshot.value().outbox.empty()) {
                std::ofstream done(configPath.string() + ".done"); done << nlohmann::json{{"id", id}, {"path", codec::pathText(progress.value().path)}}.dump();
                // Keep the sender alive until the receiver confirms the final encrypted ACK was itself receipted.
                // Closing immediately can discard the sender's queued transport receipt (a legitimate recoverable loss).
                return !sending || std::filesystem::exists(configPath.string() + ".stop");
            }
            return false;
        }, 45);
        app.shutdown(); return 0;
    } catch (const std::exception& error) {
        std::ofstream log(std::string(argv[1]) + ".error"); log << error.what();
        std::cerr << error.what() << '\n'; return 1;
    }
}
