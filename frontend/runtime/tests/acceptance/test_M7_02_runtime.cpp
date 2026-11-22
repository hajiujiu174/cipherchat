#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "file_fixture.hpp"
#include "business_worker.hpp"
using namespace ccfw;

namespace {
// Drive the same runtime as MainWindow without QApplication or widgets.
struct RuntimeClient {
    gui::BusinessWorker worker;
    gui::RestoredView restored;
    bool online = false;
    std::map<std::string, ChatRecord> messages;
    std::vector<std::string> errors;
    // 取出测试。
    void drain() {
        for (const auto& event : worker.drain()) {
            if (const auto* view = std::get_if<gui::RestoredView>(&event)) {
                restored = *view;
                for (const auto& record : view->history) messages[record.messageId] = record;
                continue;
            }
            const auto* client = std::get_if<ClientEvent>(&event);
            if (!client) continue;
            if (const auto* account = std::get_if<AccountEvent>(client))
                online = account->phase == AccountPhase::Online;
            else if (const auto* record = std::get_if<ChatRecord>(client))
                messages[record->messageId] = *record;
            else if (const auto* error = std::get_if<Error>(client))
                errors.push_back(error->message);
        }
    }
    // 获取测试对应的 received，供调用方读取或参与计算。
    bool received(const std::string& text) const {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& item) { return item.second.text == text; });
    }
    // 获取测试对应的 acknowledged，供调用方读取或参与计算。
    bool acknowledged(const std::string& text) const {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& item) {
            return item.second.text == text && item.second.state == DeliveryState::RecipientReceived;
        });
    }
};
}
// 验证用例“M7-02 runtime migration retains account scope and history without Qt”所描述的行为。
TEST_CASE("M7-02 runtime migration retains account scope and history without Qt") {
    const auto directory = test::tempDirectory("M7-02-runtime");
    const auto serverDirectory = test::tempDirectory("M7-02-server");
    std::string scope;
    std::uint16_t originalPort = 0;
    std::int64_t aliceUid = 0, bobUid = 0;
    for (int round = 0; round < 2; ++round) {
        test::RealServer server(serverDirectory);
        const auto port = server.options().port;
        if (!round) {
            originalPort = port;
            scope = "127.0.0.1:" + std::to_string(port) + "/localhost";
        } else {
            REQUIRE(port != originalPort);
        }
        RuntimeClient alice, bob;
        // 验证“authenticate”场景中的关键结果，断言失败时由测试框架报告。
        auto authenticate = [&](RuntimeClient& client, const char* name, const char* profile) {
            gui::Authenticate request;
            request.connection = server.options();
            request.username = name; request.password = "password123"; request.dataDirectory = directory / profile;
            request.registerNew = round == 0; request.accountServerId = round ? scope : "";
            REQUIRE(client.worker.post(std::move(request)));
        };
        // 推进测试。
        auto pump = [&] { alice.drain(); bob.drain(); };
        authenticate(alice, "runtime_alice", "alice"); authenticate(bob, "runtime_bob", "bob");
        REQUIRE(test::eventually([&] {
            pump(); return alice.online && bob.online && alice.restored.uid > 0 && bob.restored.uid > 0;
        }, 15000));
        if (round) {
            CHECK(alice.restored.uid == aliceUid);
            CHECK(bob.restored.uid == bobUid);
            for (const auto* client : {&alice, &bob}) {
                CHECK(client->received("Alice migration 0")); CHECK(client->received("Bob migration 0"));
            }
        } else {
            aliceUid = alice.restored.uid;
            bobUid = bob.restored.uid;
            CHECK(aliceUid != bobUid);
        }
        const auto a = "Alice migration " + std::to_string(round), b = "Bob migration " + std::to_string(round);
        REQUIRE(alice.worker.post(SendText{"runtime_bob", a, 0, ""}));
        REQUIRE(bob.worker.post(SendText{"runtime_alice", b, 0, ""}));
        REQUIRE(test::eventually([&] { pump(); return alice.received(b) && bob.received(a) && alice.acknowledged(a) && bob.acknowledged(b); }, 15000));
        for (auto* client : {&alice, &bob}) {
            for (const auto& error : client->errors) { INFO(error); CHECK(false); }
            CHECK(client->errors.empty());
            client->worker.requestStop(); client->worker.join();
            CHECK_FALSE(client->worker.post(gui::Disconnect{}));
        }
    }
    for (const auto* profile : {"alice", "bob"}) {
        std::size_t count = 0;
        for (const auto& item : std::filesystem::directory_iterator(directory / profile)) if (item.is_directory()) ++count;
        CHECK(count == 1);
    }
}
