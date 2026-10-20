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
    std::vector<PeerIdentity> pending;
    std::vector<std::string> errors;
    int confirmations = 0;
    void drain() {
        for (auto& event : worker.drain()) {
            if (auto* view = std::get_if<gui::RestoredView>(&event)) {
                restored = *view;
                for (const auto& record : view->history) messages[record.messageId] = record;
                continue;
            }
            auto& client = std::get<ClientEvent>(event);
            if (auto* account = std::get_if<AccountEvent>(&client)) {
                online = account->phase == AccountPhase::Online;
                if (online) REQUIRE(!restored.ownIdentity.signingPublicKey.empty());
            }
            else if (auto* record = std::get_if<ChatRecord>(&client)) messages[record->messageId] = *record;
            else if (auto* identity = std::get_if<PeerIdentity>(&client)) pending.push_back(*identity);
            else if (auto* error = std::get_if<Error>(&client); error && error->code != ErrorCode::IdentityChanged) errors.push_back(error->message);
        }
    }
    void confirmAgainst(const RuntimeClient& other) {
        if (other.restored.ownIdentity.signingPublicKey.empty()) return;
        for (const auto& identity : pending) {
            REQUIRE(identity.username == other.restored.ownIdentity.username);
            REQUIRE(identity.signingPublicKey == other.restored.ownIdentity.signingPublicKey);
            REQUIRE(identity.dhPublicKey == other.restored.ownIdentity.dhPublicKey);
            if (identity.manuallyVerified) continue;
            REQUIRE(worker.post(gui::ConfirmIdentity{identity})); ++confirmations;
        }
        pending.clear();
    }
    bool received(const std::string& text) const {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& item) { return item.second.text == text; });
    }
    bool acknowledged(const std::string& text) const {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& item) {
            return item.second.text == text && item.second.state == DeliveryState::RecipientReceived;
        });
    }
};
}
TEST_CASE("M7-02 runtime migration retains profile identity trust and history without Qt") {
    test::RealServer server; const auto directory = test::tempDirectory("M7-02-runtime");
    const auto scope = "127.0.0.1:" + std::to_string(server.options().port) + "/localhost";
    PeerIdentity aliceIdentity, bobIdentity;
    for (int round = 0; round < 2; ++round) {
        RuntimeClient alice, bob;
        auto authenticate = [&](RuntimeClient& client, const char* name, const char* profile) {
            gui::Authenticate request;
            request.connection = server.options(); request.connection.host = round ? "127.0.0.2" : "127.0.0.1";
            request.username = name; request.password = "password123"; request.dataDirectory = directory / profile;
            request.registerNew = round == 0; request.accountServerId = round ? scope : "";
            REQUIRE(client.worker.post(std::move(request)));
        };
        auto pump = [&] { alice.drain(); bob.drain(); alice.confirmAgainst(bob); bob.confirmAgainst(alice); };
        authenticate(alice, "runtime_alice", "alice"); authenticate(bob, "runtime_bob", "bob");
        REQUIRE(test::eventually([&] {
            pump(); return alice.online && bob.online && !alice.restored.ownIdentity.signingPublicKey.empty() && !bob.restored.ownIdentity.signingPublicKey.empty();
        }, 15000));
        if (!round) { aliceIdentity = alice.restored.ownIdentity; bobIdentity = bob.restored.ownIdentity; }
        else {
            CHECK(alice.restored.ownIdentity.signingPublicKey == aliceIdentity.signingPublicKey);
            CHECK(alice.restored.ownIdentity.dhPublicKey == aliceIdentity.dhPublicKey);
            CHECK(bob.restored.ownIdentity.signingPublicKey == bobIdentity.signingPublicKey);
            CHECK(bob.restored.ownIdentity.dhPublicKey == bobIdentity.dhPublicKey);
            for (const auto* client : {&alice, &bob}) {
                CHECK(client->received("Alice migration 0")); CHECK(client->received("Bob migration 0"));
            }
        }
        const auto a = "Alice migration " + std::to_string(round), b = "Bob migration " + std::to_string(round);
        REQUIRE(alice.worker.post(SendText{"runtime_bob", a, 0, ""}));
        REQUIRE(bob.worker.post(SendText{"runtime_alice", b, 0, ""}));
        REQUIRE(test::eventually([&] { pump(); return alice.received(b) && bob.received(a) && alice.acknowledged(a) && bob.acknowledged(b); }, 15000));
        for (auto* client : {&alice, &bob}) {
            for (const auto& error : client->errors) { INFO(error); CHECK(false); }
            CHECK(client->errors.empty());
            if (round) CHECK(client->confirmations == 0); else CHECK(client->confirmations > 0);
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
