#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ccfw/local_store.hpp"
#include "ccfw/session_service.hpp"

#include <stdexcept>

namespace {

using namespace ccfw;
using namespace cc;

// 内存版 ILocalStore：语义与 SQLite 版一致（revision 严格 +1、失败零变化）
class FakeStore final : public ILocalStore {
public:
    // 打开测试。
    Result<> open(const std::filesystem::path&, const AccountScope& scope) override {
        snap_.account = scope;
        opened_ = true;
        return Result<>::success(Unit{});
    }
    // 加载测试。
    Result<ClientSnapshot> load() override {
        if (!opened_) {
            return Result<ClientSnapshot>::failure(
                {ErrorCode::InvalidState, "M1-03", "未打开"});
        }
        return Result<ClientSnapshot>::success(snap_);
    }
    // 提交测试。
    Result<> commit(std::uint64_t expectedRevision,
                    const ClientSnapshot& next) override {
        if (!opened_ || next.revision != expectedRevision + 1 ||
            snap_.revision != expectedRevision) {
            return Result<>::failure(
                {ErrorCode::Conflict, "M1-03", "revision 冲突"});
        }
        snap_ = next;
        return Result<>::success(Unit{});
    }
    // 关闭测试。
    void close() noexcept override { opened_ = false; }

    ClientSnapshot snap_;

private:
    bool opened_ = false;
};

}

// 验证用例“M1-03 rejects a retired message format before changing history”所描述的行为。
TEST_CASE("M1-03 rejects a retired message format before changing history") {
    FakeStore store;
    REQUIRE(store.open({}, {"server", "alice"}));
    SessionService session(store);
    const nlohmann::json retired{{"ciphertext", "older-envelope"}};
    WireEnvelope packet{"old-id", "bob", "alice", "chat", cc::base64Encode(retired.dump()), 0, 1};
    const auto rejected = session.decryptAndCommit(packet);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == ErrorCode::ProtocolError);
    const auto state = store.load().value();
    CHECK(state.revision == 0);
    CHECK(state.receivedMessages.empty());
    CHECK(state.history.empty());
}

// 验证用例“M1-03 validates ordinary envelope fields before the outbox commit”所描述的行为。
TEST_CASE("M1-03 validates ordinary envelope fields before the outbox commit") {
    FakeStore store;
    REQUIRE(store.open({}, {"server", "alice"}));
    SessionService session(store);
    PlainEnvelope draft{"m1", "", "alice", "bob", "chat", "普通消息", 0, 1};
    auto invalid = draft;
    SUBCASE("wrong sender") { invalid.from = "other"; }
    SUBCASE("self target") { invalid.to = "alice"; }
    SUBCASE("unknown kind") { invalid.kind = "legacy"; }
    SUBCASE("empty message ID") { invalid.messageId.clear(); }
    SUBCASE("bad timestamp") { invalid.timestamp = -1; }
    const auto rejected = session.encryptAndCommit(invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == ErrorCode::InvalidArgument);
    CHECK(store.load().value().revision == 0);
    const auto sent = session.encryptAndCommit(draft);
    REQUIRE(sent);
    const auto duplicate = session.encryptAndCommit(draft);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().code == ErrorCode::Conflict);
    const auto state = store.load().value();
    REQUIRE(state.outbox.size() == 1);
    REQUIRE(state.history.size() == 1);
    CHECK(state.outbox.front().payloadBase64 == sent.value().payloadBase64);
    CHECK(state.history.front().text == draft.plaintext);
    CHECK(state.revision == 1);
}
