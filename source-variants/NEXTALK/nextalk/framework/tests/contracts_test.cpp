#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ccfw/application.hpp"
#include "ccfw/server_services.hpp"
#include <type_traits>

TEST_CASE("Result distinguishes explicit failure from success") {
    auto bad = ccfw::pending<int>("M0-01");
    CHECK_FALSE(bad.ok());
    CHECK(bad.error().code == ccfw::ErrorCode::NotImplemented);
    CHECK(bad.error().task == "M0-01");
    CHECK_THROWS_AS(bad.value(), std::bad_variant_access);
    auto good = ccfw::Result<int>::success(42);
    REQUIRE(good.ok());
    CHECK(good.value() == 42);
    CHECK_THROWS_AS(good.error(), std::bad_variant_access);
}
TEST_CASE("Fresh DTOs contain no identity or implicit completion") {
    ccfw::ClientSnapshot state;
    CHECK_FALSE(state.identity.has_value());
    CHECK(state.revision == 0);
    CHECK(state.outbox.empty());
    CHECK_FALSE(ccfw::FileProgress{}.complete);
    CHECK(ccfw::ChatRecord{}.state == ccfw::DeliveryState::Queued);
    static_assert(std::has_virtual_destructor_v<ccfw::ITransport>);
    static_assert(std::has_virtual_destructor_v<ccfw::ILocalStore>);
}
TEST_CASE("Whole framework assembles without network or account side effects") {
    struct Events final : ccfw::IEventSink {
        int count = 0;
        void publish(ccfw::ClientEvent) override { ++count; }
    } events;
    ccfw::PlatformSecretProtector protector;
    ccfw::SqliteLocalStore store(protector);
    ccfw::TlsTransport transport;
    ccfw::SessionService sessions(store);
    ccfw::AccountService account(transport, store);
    ccfw::ChatService chat(transport, sessions, store);
    ccfw::FileTransferService files(transport, store, sessions);
    ccfw::GroupService groups(chat, transport, {"test-server", "alice"});
    ccfw::ClientApplication application(transport, account, chat, files, groups, events);
    CHECK(account.phase() == ccfw::AccountPhase::Disconnected);
    application.shutdown();
    application.shutdown();
    CHECK(account.phase() == ccfw::AccountPhase::Disconnected);
    CHECK(events.count == 0);
}
// 这些是骨架契约测试，不测试“所有功能都返回 pending”，实现后无需删测试才能变绿。
// 当前业务 TODO 是否未完成由 tasks.json + 任务卡报告，不由绿色冒烟暗示完成。
