#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "client_fixture.hpp"
#include "ccfw/delivery_service.hpp"
#include "../compatibility/identity_card.hpp"
#include "../../src/feature_codec.hpp"
#include "common/storage/sqlite_helpers.hpp"
#include "../compatibility/process_io.hpp"
#include <set>
using namespace ccfw;
// 验证用例“M10 zero-byte same-name delivery and unknown extensions”所描述的行为。
TEST_CASE("M10 zero-byte same-name delivery and unknown extensions") {
    test::RealServer server; test::RealClient a("zero_a"), b("zero_b"); a.registerAndLogin(server.options()); b.registerAndLogin(server.options()); DeliveryService sender(a.store, a.chat, a.files), receiver(b.store, b.chat, b.files);
    std::vector<std::filesystem::path> paths;
    for (unsigned n = 0; n < 16; ++n) { auto dir = a.directory / std::to_string(n); std::filesystem::create_directory(dir); auto p = dir / "same-name.txt"; std::ofstream out(p); paths.push_back(p); }
    REQUIRE(sender.create({"sixteen-zero", b.name, "十六个同名空文件", "零字节按已完成文件计进度", "", paths}));
    CHECK_FALSE(receiver.confirm({"sixteen-zero", "early"}));
    REQUIRE(test::eventually([&] { a.application.tick(); b.application.tick(); REQUIRE(sender.tick()); REQUIRE(receiver.tick()); auto s = b.store.load(); return s && !s.value().features.bundles.empty() && s.value().features.bundles[0].state == BundleState::Complete; }, 20000));
    auto incoming = b.store.load().value().features.bundles[0]; CHECK(deliveryProgress(incoming) == 1); CHECK(incoming.items.size() == 16);
    const auto transferred = b.store.load().value().transfers;
    std::set<std::filesystem::path> outputs; for (const auto& t : transferred) { CHECK(t.complete); outputs.insert(t.finalPath); } CHECK(outputs.size() == 16);
    REQUIRE(receiver.confirm({incoming.id, incoming.manifestDigest}));
    SendText fallback{b.name, "未来扩展仍按普通文字展示", 0, ""}; fallback.extensions = {{"delivery", {{"version", 99}, {"future", "unknown"}}}};
    const auto id = a.chat.sendText(fallback); REQUIRE(id); a.settle(b, id.value()); CHECK(b.store.load().value().history.back().text == fallback.text);
}
#ifdef CCFW_FEATURE_LEGACY_PATH
// 验证用例“M10 imports history written by the frozen client and keeps its recovery archive”所描述的行为。
TEST_CASE("M10 imports history written by the frozen client and keeps its recovery archive") {
    const auto directory = test::tempDirectory("frozen-import");
    const auto config = directory / "legacy.json";
    const auto dbPath = directory / "client.db";
    const std::string text = "旧版聊天记录可以恢复";
    compat::write(config, {{"directory", codec::pathText(directory)}, {"username", "interop_b"},
                          {"peer", "interop_a"}, {"offline_snapshot", text}});
    compat::Process legacy(std::filesystem::path(CCFW_FEATURE_LEGACY_PATH), config);
    REQUIRE(test::eventually([&] { return !legacy.running(); }));
    REQUIRE(compat::read(config.string() + ".status").value("snapshot_written", false));

    PlatformSecretProtector protector;
    SqliteLocalStore store(protector);
    REQUIRE(store.open(dbPath, {"real-test-server", "interop_b"}));
    auto state = store.load().value();
    REQUIRE(state.history.size() == 1);
    CHECK(state.history.front().text == text);
    CHECK(state.history.front().from == "interop_a");
    CHECK(state.history.front().messageId == "legacy-fixture");
    CHECK(state.revision == 2);
    auto backup = dbPath;
    backup += ".before-ordinary.bak";
    REQUIRE(std::filesystem::is_regular_file(backup));
    const auto size = std::filesystem::file_size(backup);
    REQUIRE(size > 0);
    state.features.workspace.drafts["peer:interop_a"] = "新版草稿";
    const auto revision = state.revision++;
    REQUIRE(store.commit(revision, state));
    store.close();
    REQUIRE(store.open(dbPath, {"real-test-server", "interop_b"}));
    const auto restored = store.load().value();
    CHECK(restored.history.front().messageId == "legacy-fixture");
    CHECK(restored.features.workspace.drafts.at("peer:interop_a") == "新版草稿");
    CHECK(std::filesystem::file_size(backup) == size);
}
#endif
// 验证用例“M10 real TLS delivery stages and failed commit”所描述的行为。
TEST_CASE("M10 real TLS delivery stages and failed commit") {
    test::RealServer server; test::RealClient a("trace_a"), b("trace_b");
    a.registerAndLogin(server.options()); b.registerAndLogin(server.options());
    auto first = a.chat.sendText({b.name, "多行消息\n真实发送流程", 0, ""}); REQUIRE(first);
    auto events = a.chat.takeStageEvents(); REQUIRE(events.size() == 2);
    CHECK(events[0].stage == MessageStage::LocalQueued); CHECK(events[1].stage == MessageStage::TransportQueued);
    a.settle(b, first.value());
    std::set<MessageStage> stages;
    for (const auto& e : a.chat.stages()) if (e.messageId == first.value()) { stages.insert(e.stage); CHECK(e.elapsedMs >= 0); }
    CHECK(stages.count(MessageStage::ServerAccepted)); CHECK(stages.count(MessageStage::TransportQueued)); CHECK(stages.count(MessageStage::RecipientReceived));
    const auto history = a.store.load().value().history.size(); a.store.fail = true;
    CHECK_FALSE(a.chat.sendText({b.name, "保存失败不能成功", 0, ""}));
    auto failed = a.chat.takeStageEvents(); REQUIRE_FALSE(failed.empty()); CHECK(failed.back().stage == MessageStage::Failed);
    CHECK(a.store.load().value().history.size() == history); a.store.fail = false;
    REQUIRE(a.chat.handleAck(first.value(), DeliveryState::RecipientReceived, b.name)); CHECK(a.chat.takeStageEvents().empty());
}
// 验证用例“M10 historical identity-card importer binds two complete keys and scope”所描述的行为。
TEST_CASE("M10 historical identity-card importer binds two complete keys and scope") {
    PeerIdentity p{"friend", cc::crypto::generateEd25519().pub, cc::crypto::generateX25519().pub, false};
    const auto hash = identityFingerprint(p); REQUIRE(hash.size() == 64);
    auto lower = hash; for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(compareFingerprint(p, "\n " + lower.substr(0, 32) + " \t" + lower.substr(32)));
    CHECK_FALSE(compareFingerprint(p, hash.substr(0, 63))); CHECK_FALSE(compareFingerprint(p, hash + "0"));
    auto encoded = exportIdentityCard({{"server-a", p.username}, p}); REQUIRE(encoded);
    auto imported = importIdentityCard(encoded.value(), {"server-a", p.username}); REQUIRE(imported);
    CHECK(imported.value().identity.signingPublicKey == p.signingPublicKey); CHECK_FALSE(imported.value().identity.manuallyVerified);
    CHECK_FALSE(importIdentityCard(encoded.value(), {"server-b", p.username})); CHECK_FALSE(importIdentityCard(encoded.value(), {"server-a", "other"}));
    CHECK_FALSE(importIdentityCard("https://not-an-identity-card.invalid", {"server-a", p.username}));
    CHECK_FALSE(importIdentityCard(encoded.value().substr(0, 35), {"server-a", p.username}));
    auto changed = p; changed.dhPublicKey[0] ^= 1; CHECK_FALSE(validateIdentityConfirmation(p, changed)); CHECK(validateIdentityConfirmation(p, p));
}
// 验证用例“M10 multi-file delivery is durable idempotent and manually verified”所描述的行为。
TEST_CASE("M10 multi-file delivery is durable idempotent and manually verified") {
    test::RealServer server; test::RealClient a("bundle_a"), b("bundle_b");
    a.registerAndLogin(server.options()); b.registerAndLogin(server.options()); DeliveryService send(a.store, a.chat, a.files), receive(b.store, b.chat, b.files);
    const auto source = a.directory / "课程设计_长文件名.bin", empty = a.directory / "空文件.txt";
    { std::ofstream out(source, std::ios::binary); out << std::string(4 * 1024 * 1024 + 15, 'q'); }
    { std::ofstream out(empty, std::ios::binary); }
    CreateDeliveryBundle request{"operation-1", b.name, "第一次交付", "正文供旧客户端阅读", "", {source, empty}};
    auto created = send.create(request); REQUIRE(created); CHECK(send.create(request).value() == created.value());
    auto wrong = request; wrong.title = "已修改"; CHECK_FALSE(send.create(wrong)); CHECK_FALSE(receive.confirm({created.value(), "wrong"}));
    REQUIRE(test::eventually([&] {
        a.application.tick(); b.application.tick(); send.tick(); receive.tick();
        const auto s = b.store.load(); return s && !s.value().features.bundles.empty() && s.value().features.bundles.front().state == BundleState::Complete;
    }, 20000));
    const auto incoming = b.store.load().value().features.bundles.front(); REQUIRE(incoming.items.size() == 2);
    CHECK(deliveryProgress(incoming) == 1.0); CHECK_FALSE(incoming.manuallyConfirmed); CHECK(a.store.load().value().transfers.size() == 2);
    CHECK_FALSE(send.confirm({incoming.id, incoming.manifestDigest})); CHECK_FALSE(receive.confirm({incoming.id, "bad"}));
    auto transfers = b.store.load().value().transfers;
    const auto large = std::find_if(transfers.begin(), transfers.end(), [](const auto& t) { return t.totalBytes > 0; }); REQUIRE(large != transfers.end());
    { std::fstream edit(large->finalPath, std::ios::binary | std::ios::in | std::ios::out); edit.put('x'); }
    CHECK_FALSE(receive.confirm({incoming.id, incoming.manifestDigest}));
    { std::fstream edit(large->finalPath, std::ios::binary | std::ios::in | std::ios::out); edit.put('q'); }
    REQUIRE(receive.confirm({incoming.id, incoming.manifestDigest})); REQUIRE(receive.confirm({incoming.id, incoming.manifestDigest}));
    REQUIRE(test::eventually([&] { a.application.tick(); b.application.tick(); send.tick(); receive.tick(); return a.store.load().value().features.bundles.front().manuallyConfirmed; }));
    const auto history = b.store.load().value().history;
    CHECK(std::count_if(history.begin(), history.end(), [](const auto& h) { return h.messageId.starts_with("delivery-confirm:"); }) == 1);
    b.store.close(); REQUIRE(b.store.open(b.directory / "client.db", {"real-test-server", b.name})); CHECK(b.store.load().value().features.bundles.front().manuallyConfirmed);
    request.operationId = "operation-2"; request.previousId = created.value(); request.title = "第二次交付";
    REQUIRE(send.create(request)); CHECK(a.store.load().value().features.bundles.back().previousId == created.value());
    REQUIRE(send.cancel({request.operationId})); CHECK(a.store.load().value().features.bundles.back().state == BundleState::Cancelled);
}
// 验证用例“M10 legacy format1 writes preserve sidecar and cache checks both ciphertexts”所描述的行为。
TEST_CASE("M10 legacy format1 writes preserve sidecar and cache checks both ciphertexts") {
    const auto dir = test::tempDirectory("features-store"); PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(dir / "client.db", {"scope", "alice"})); auto s = store.load().value();
    s.features.workspace = {"bob", 0, {"bob"}, {{"peer:bob", "未发送草稿"}}}; s.revision++; REQUIRE(store.commit(0, s));
    sql::Database db; db.open(codec::pathText(dir / "client.db"));
    auto legacy = nlohmann::json(s); legacy["revision"] = 2;
    auto bytes = nlohmann::json::to_cbor(legacy); auto sealed = protector.seal({reinterpret_cast<const char*>(bytes.data()), bytes.size()}); REQUIRE(sealed);
    sql::Statement update(db.value, "UPDATE client_state SET revision=2,protected_snapshot=?1 WHERE singleton=1"); update.blob(1, sealed.value()); update.step();
    auto reloaded = store.load(); REQUIRE(reloaded); CHECK(reloaded.value().features.workspace.drafts.at("peer:bob") == "未发送草稿");
    sql::exec(db.value, "UPDATE client_features SET protected_features=x'0000' WHERE singleton=1"); CHECK_FALSE(store.load());
    auto next = reloaded.value(); next.revision++; CHECK_FALSE(store.commit(2, next));
    sql::Statement rev(db.value, "SELECT revision FROM client_state"); REQUIRE(rev.step()); CHECK(rev.number(0) == 2);
}
