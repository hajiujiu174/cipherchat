// Current NEXTALK runtime over certificate-verified TLS; no GUI or old E2EE client.
#include <doctest/doctest.h>
#include "business_worker.hpp"
#include "server/net/tls_server.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
using namespace ccfw;
namespace fs = std::filesystem;
fs::path testDirectory() {
    auto path = fs::current_path() / ("ordinary-integration-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(path); return path;
}
TEST_CASE("NEXTALK protected data reads historical snapshots and writes the current brand") {
#ifdef _WIN32
    PlatformSecretProtector protector;
    const cc::Bytes text = "synthetic-rename-migration-fixture";
    const auto sealed = protector.seal(text);
    REQUIRE(sealed);
    const auto roundTrip = protector.unseal(sealed.value());
    REQUIRE(roundTrip);
    CHECK(roundTrip.value() == text);
    const std::string entropy = "Cipher" "Chat/local-snapshot/DPAPI/v1";
    DATA_BLOB input{DWORD(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))};
    DATA_BLOB encrypted{};
    REQUIRE(CryptProtectData(&input, L"historical fixture", &extra, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &encrypted));
    cc::Bytes old("CCDPAPI1");
    for (int shift = 24; shift >= 0; shift -= 8) old.push_back(static_cast<char>(encrypted.cbData >> shift));
    old.append(reinterpret_cast<char*>(encrypted.pbData), encrypted.cbData);
    LocalFree(encrypted.pbData);
    const auto restored = protector.unseal(old);
    REQUIRE(restored);
    CHECK(restored.value() == text);
#endif
}
struct TestServer {
    fs::path directory = testDirectory();
    cc::server::Storage db{(directory / "server.db").string()};
    cc::server::Router router{db};
    cc::server::TlsServer tls{router, (directory / "cert.pem").string(), (directory / "key.pem").string()};
    std::future<bool> listener;
    explicit TestServer(bool cacheEnabled = false) {
        router.setFileCacheEnabled(cacheEnabled);
        listener = std::async(std::launch::async, [this] { return tls.listen(0, "127.0.0.1"); });
    }
    ~TestServer() { tls.stop(); if (listener.valid()) listener.wait(); }
    ConnectionOptions options() { return {"127.0.0.1", tls.port(), directory / "cert.pem", "localhost", 5000}; }
};
struct ClientHarness {
    gui::BusinessWorker worker;
    gui::RestoredView restored;
    bool online = false;
    std::map<std::string, ChatRecord> messages;
    std::map<std::string, FileProgress> files;
    std::vector<Error> errors;
    std::vector<nlohmann::json> social;
    void drain() {
        for (auto& event : worker.drain()) {
            if (const auto* view = std::get_if<gui::RestoredView>(&event)) {
                restored = *view;
                for (const auto& message : view->history) messages[message.messageId] = message;
                for (const auto& file : view->files) files[file.fileId] = file;
            } else if (const auto* client = std::get_if<ClientEvent>(&event)) {
                if(const auto* item=std::get_if<SocialEvent>(client))social.push_back(item->value);
                if (const auto* account = std::get_if<AccountEvent>(client)) online = account->phase == AccountPhase::Online;
                else if (const auto* message = std::get_if<ChatRecord>(client)) messages[message->messageId] = *message;
                else if (const auto* file = std::get_if<FileProgress>(client)) files[file->fileId] = *file;
                else if (const auto* error = std::get_if<Error>(client)) errors.push_back(*error);
            }
        }
    }
    void authenticate(TestServer& server, const std::string& name, bool registerNew = true,
                      const std::string& password = "IntegrationTest-2026!") {
        gui::Authenticate request;
        request.connection = server.options(); request.dataDirectory = server.directory / (name + "-client");
        request.username = name; request.password = password; request.registerNew = registerNew; request.directEnabled = false;
        REQUIRE(worker.post(std::move(request)));
    }
    bool hasMessage(const std::string& text, DeliveryState state = DeliveryState::RecipientReceived) const {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& item) {
            return item.second.text == text && item.second.state == state;
        });
    }
};
template<class Predicate>
bool eventually(ClientHarness& a, ClientHarness& b, Predicate predicate, int timeoutMs = 15000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do { a.drain(); b.drain(); if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < until);
    return false;
}
void requireNoErrors(const ClientHarness& client) {
    for (const auto& error : client.errors) { INFO(error.task << ": " << error.message); CHECK(false); }
}
cc::Bytes readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary); REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
}
TEST_CASE("E2E-1 current runtime registers and exchanges ordinary messages over verified TLS") {
    TestServer server; REQUIRE(server.tls.waitReady(5000));
    ClientHarness alice, bob;
    alice.authenticate(server, "alice"); bob.authenticate(server, "bob");
    REQUIRE(eventually(alice, bob, [&] { return alice.online && bob.online; }));
    CHECK(alice.restored.ownIdentity.signingPublicKey.empty()); CHECK(bob.restored.ownIdentity.dhPublicKey.empty());
    std::string longText; for (int i = 0; i < 2500; ++i) longText += "长";
    for (int i = 0; i < 6; ++i) {
        REQUIRE(alice.worker.post(SendText{"bob", "A->B " + std::to_string(i), 0, "", ""}));
        REQUIRE(bob.worker.post(SendText{"alice", "B->A " + std::to_string(i), 0, "", ""}));
    }
    REQUIRE(alice.worker.post(SendText{"bob", longText, 0, "", ""}));
    REQUIRE(eventually(alice, bob, [&] { return bob.hasMessage(longText) && alice.hasMessage(longText) &&
        alice.messages.size() == 13 && bob.messages.size() == 13; }));
    for (int i = 0; i < 6; ++i) {
        CHECK(bob.hasMessage("A->B " + std::to_string(i))); CHECK(alice.hasMessage("B->A " + std::to_string(i)));
    }
    requireNoErrors(alice); requireNoErrors(bob);
}
TEST_CASE("E2E-2 offline message survives disconnect and is acknowledged after fresh login") {
    TestServer server; REQUIRE(server.tls.waitReady(5000));
    ClientHarness alice, bob;
    alice.authenticate(server, "alice"); bob.authenticate(server, "bob");
    REQUIRE(eventually(alice, bob, [&] { return alice.online && bob.online; }));
    REQUIRE(bob.worker.post(gui::Disconnect{}));
    REQUIRE(eventually(alice, bob, [&] { return !bob.online && !server.router.isOnline("bob"); }));
    REQUIRE(alice.worker.post(SendText{"bob", "离线消息补投", 0, "", ""}));
    REQUIRE(eventually(alice, bob, [&] { return alice.hasMessage("离线消息补投", DeliveryState::ServerAccepted); }));
    CHECK_FALSE(bob.hasMessage("离线消息补投"));
    bob.authenticate(server, "bob", false);
    REQUIRE(eventually(alice, bob, [&] { return bob.online && bob.hasMessage("离线消息补投") && alice.hasMessage("离线消息补投"); }));
    REQUIRE(bob.worker.post(SendText{"alice", "补投收悉", 0, "", ""}));
    REQUIRE(eventually(alice, bob, [&] { return alice.hasMessage("补投收悉") && bob.hasMessage("补投收悉"); }));
    requireNoErrors(alice); requireNoErrors(bob);
}
TEST_CASE("E2E-3 wrong password is rejected and a fresh device logs in without identity keys") {
    TestServer server; REQUIRE(server.tls.waitReady(5000));
    ClientHarness original, wrong;
    original.authenticate(server, "carol"); REQUIRE(eventually(original, wrong, [&] { return original.online; }));
    wrong.authenticate(server, "carol", false, "DefinitelyWrongPassword!");
    REQUIRE(eventually(original, wrong, [&] { return !wrong.errors.empty(); }));
    CHECK_FALSE(wrong.online); CHECK(original.online); CHECK(server.router.isOnline("carol"));
    bool rejectedPassword = false;
    for (const auto& error : wrong.errors) if (error.code == ErrorCode::AuthenticationFailed) rejectedPassword = true;
    CHECK(rejectedPassword);
    ClientHarness fresh;
    gui::Authenticate request;
    request.connection = server.options(); request.dataDirectory = server.directory / "new-device";
    request.username = "carol"; request.password = "IntegrationTest-2026!"; request.directEnabled = false;
    REQUIRE(fresh.worker.post(std::move(request))); REQUIRE(eventually(fresh, wrong, [&] { return fresh.online; }));
    CHECK(fresh.restored.ownIdentity.signingPublicKey.empty()); requireNoErrors(fresh);
}
TEST_CASE("E2E-4 production file engine relays multiple chunks with complete ACK and identical bytes") {
    TestServer server; REQUIRE(server.tls.waitReady(5000));
    ClientHarness alice, bob;
    alice.authenticate(server, "alice"); bob.authenticate(server, "bob");
    REQUIRE(eventually(alice, bob, [&] { return alice.online && bob.online; }));
    const auto source = server.directory / "report.bin";
    cc::Bytes content(300 * 1024 + 17, '\0');
    for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<char>((i * 31 + 0x5a) & 255);
    { std::ofstream output(source, std::ios::binary); REQUIRE(output.good()); output.write(content.data(), content.size()); REQUIRE(output.good()); }
    REQUIRE(alice.worker.post(gui::SendFile{"bob", source}));
    REQUIRE(eventually(alice, bob, [&] { return !alice.files.empty() && !bob.files.empty() &&
        alice.files.begin()->second.complete && bob.files.begin()->second.complete; }, 30000));
    const auto& sent = alice.files.begin()->second; const auto& received = bob.files.begin()->second;
    CHECK(sent.outgoing); CHECK_FALSE(received.outgoing); CHECK(sent.fileId == received.fileId);
    CHECK(sent.authenticatedBytes == content.size()); CHECK(received.authenticatedBytes == content.size());
    CHECK(readBytes(received.path) == content); requireNoErrors(alice); requireNoErrors(bob);
}

TEST_CASE("E2E-5 server cached file reaches an offline recipient after reconnect without premature completion") {
    TestServer server(true); REQUIRE(server.tls.waitReady(5000));
    ClientHarness alice, bob;
    alice.authenticate(server, "alice"); bob.authenticate(server, "bob");
    REQUIRE(eventually(alice, bob, [&] { return alice.online && bob.online; }));
    REQUIRE(bob.worker.post(gui::Disconnect{}));
    REQUIRE(eventually(alice, bob, [&] { return !bob.online && !server.router.isOnline("bob"); }));
    const auto source = server.directory / "offline-cache.bin";
    cc::Bytes content(300 * 1024 + 37, '\0');
    for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<char>((i * 37 + 0xa5) & 255);
    { std::ofstream output(source, std::ios::binary); REQUIRE(output.good()); output.write(content.data(), content.size()); REQUIRE(output.good()); }
    REQUIRE(alice.worker.post(gui::SendFile{"bob", source}));
    REQUIRE(eventually(alice, bob, [&] { return !alice.files.empty() && alice.files.begin()->second.cacheUploaded; }, 30000));
    CHECK_FALSE(alice.files.begin()->second.complete);
    CHECK(bob.files.empty());
    bob.authenticate(server, "bob", false);
    REQUIRE(eventually(alice, bob, [&] { return bob.online && !bob.files.empty() &&
        bob.files.begin()->second.complete && alice.files.begin()->second.complete; }, 30000));
    CHECK(readBytes(bob.files.begin()->second.path) == content);
    CHECK(alice.files.begin()->second.authenticatedBytes == content.size());
    requireNoErrors(alice); requireNoErrors(bob);
}
TEST_CASE("E2E-6 pause survives reconnect and group file resumes with identical bytes") {
    TestServer server(true);REQUIRE(server.tls.waitReady(5000));ClientHarness alice,bob;
    alice.authenticate(server,"alice");bob.authenticate(server,"bob");REQUIRE(eventually(alice,bob,[&]{return alice.online&&bob.online;}));
    REQUIRE(alice.worker.post(gui::CreateGroup{"文件群",{"bob"}}));
    // The group directory reaches both workers before starting the file command.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto groups=server.db.groupsForUser("alice");REQUIRE(groups.size()==1);const auto gid=groups[0].id;
    const auto source=server.directory/"group-pause.bin";cc::Bytes bytes(4*1024*1024+17,'x');for(std::size_t i=0;i<bytes.size();++i)bytes[i]=char(i*37);
    {std::ofstream f(source,std::ios::binary);f.write(bytes.data(),bytes.size());}
    REQUIRE(alice.worker.post(gui::SendFile{"bob",source,gid}));REQUIRE(eventually(alice,bob,[&]{return !alice.files.empty();}));const auto id=alice.files.begin()->first;
    REQUIRE(alice.worker.post(gui::PauseFile{id}));REQUIRE(eventually(alice,bob,[&]{return alice.files.at(id).paused;}));
    const auto before=alice.files.at(id).authenticatedBytes;std::this_thread::sleep_for(std::chrono::milliseconds(300));alice.drain();bob.drain();CHECK(alice.files.at(id).paused);CHECK(alice.files.at(id).authenticatedBytes==before);CHECK_FALSE(alice.files.at(id).complete);
    REQUIRE(alice.worker.post(gui::Disconnect{}));REQUIRE(eventually(alice,bob,[&]{return !alice.online;}));alice.authenticate(server,"alice",false);
    REQUIRE(eventually(alice,bob,[&]{return alice.online;}));CHECK(alice.files.at(id).paused);REQUIRE(alice.worker.post(gui::ResumeFile{id}));
    REQUIRE(eventually(alice,bob,[&]{return alice.files.at(id).complete&&!bob.files.empty()&&bob.files.begin()->second.complete;},60000));
    CHECK(bob.files.begin()->second.conversationGroupId==gid);CHECK(readBytes(bob.files.begin()->second.path)==bytes);requireNoErrors(alice);requireNoErrors(bob);
}
TEST_CASE("E2E-7 call relay requires consent, rejects spoofing and carries both media directions") {
    TestServer server;REQUIRE(server.tls.waitReady(5000));ClientHarness a,b,outsider;
    a.authenticate(server,"alice");b.authenticate(server,"bob");outsider.authenticate(server,"eve");REQUIRE(eventually(a,b,[&]{outsider.drain();return a.online&&b.online&&outsider.online;}));
    REQUIRE(a.worker.post(gui::AddFriend{"bob"}));std::this_thread::sleep_for(std::chrono::milliseconds(100));REQUIRE(b.worker.post(gui::RespondFriend{"alice",true}));std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send=[](ClientHarness& client,const char* action,const char* peer,nlohmann::json extra=nlohmann::json::object()){nlohmann::json v={{"op","call"},{"action",action},{"to",peer},{"call_id","synthetic-call"}};v.update(extra);return client.worker.post(gui::SocialRequest{v});};
    REQUIRE(send(a,"invite","bob"));REQUIRE(eventually(a,b,[&]{return !b.social.empty();}));CHECK(b.social.back()["from"]=="alice");CHECK(b.social.back()["action"]=="invite");
    REQUIRE(send(a,"media","bob",{{"sequence",1},{"audio","AA=="}}));REQUIRE(eventually(a,b,[&]{return !a.social.empty()&&a.social.back().value("error",std::string{})=="forbidden";}));
    REQUIRE(send(outsider,"accept","alice"));REQUIRE(eventually(a,b,[&]{outsider.drain();return !outsider.social.empty();}));CHECK(outsider.social.back()["ok"]==false);
    REQUIRE(send(b,"accept","alice"));REQUIRE(eventually(a,b,[&]{return a.social.back().value("action",std::string{})=="accept";}));
    REQUIRE(send(a,"media","bob",{{"sequence",1},{"audio","AA=="},{"image","synthetic-frame"},{"from","eve"}}));
    REQUIRE(eventually(a,b,[&]{return b.social.back().value("action",std::string{})=="media";}));CHECK(b.social.back()["from"]=="alice");CHECK(b.social.back()["audio"]=="AA==");
    REQUIRE(send(b,"media","alice",{{"sequence",1},{"audio","AQ=="}}));REQUIRE(eventually(a,b,[&]{return a.social.back().value("action",std::string{})=="media";}));CHECK(a.social.back()["from"]=="bob");
    const auto count=b.social.size();REQUIRE(send(a,"media","bob",{{"sequence",1},{"audio","REPLAY"}}));std::this_thread::sleep_for(std::chrono::milliseconds(100));a.drain();b.drain();CHECK(b.social.size()==count);
    REQUIRE(send(a,"end","bob"));REQUIRE(eventually(a,b,[&]{return b.social.back().value("action",std::string{})=="end";}));
}
