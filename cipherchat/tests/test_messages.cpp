#include <doctest/doctest.h>

#include "common/protocol/messages.hpp"

// doctest 无法直接字符串化 enum class，借用项目的 msgTypeName 提供打印。
namespace doctest {
template <>
struct StringMaker<cc::proto::MsgType> {
    static String convert(const cc::proto::MsgType& value) {
        return cc::proto::msgTypeName(value);
    }
};
}  // namespace doctest

TEST_CASE("消息类型字符串与枚举一一对应") {
    for (int t = 0; t <= static_cast<int>(cc::proto::MsgType::Error); ++t) {
        const auto type = static_cast<cc::proto::MsgType>(t);
        const char* name = cc::proto::msgTypeName(type);
        CHECK(name != nullptr);
        CHECK(cc::proto::parseMsgType(name) == type);
    }
    CHECK_FALSE(cc::proto::parseMsgType("no_such_type").has_value());
}

TEST_CASE("序列化往返：保留未知字段并还原 type/msg_id") {
    auto m = cc::proto::makeSendMsg("m-1", "bob", "chat", "QUJD", 7);
    const std::string text = cc::proto::serialize(m);
    auto back = cc::proto::deserialize(text);
    REQUIRE(back.has_value());
    CHECK(back->type == cc::proto::MsgType::SendMsg);
    CHECK(back->msgId == "m-1");
    CHECK(cc::proto::bodyStr(back->body, "to") == "bob");
    CHECK(cc::proto::bodyStr(back->body, "kind") == "chat");
    CHECK(cc::proto::bodyStr(back->body, "payload") == "QUJD");
    CHECK(cc::proto::bodyInt(back->body, "group_id") == 7);

    // 无 msg_id 的消息序列化后不含该字段
    auto plain = cc::proto::makeListUsers();
    auto back2 = cc::proto::deserialize(cc::proto::serialize(plain));
    REQUIRE(back2.has_value());
    CHECK(back2->type == cc::proto::MsgType::ListUsers);
    CHECK(back2->msgId.empty());
}

TEST_CASE("反序列化拒绝非法输入") {
    CHECK_FALSE(cc::proto::deserialize("not json").has_value());
    CHECK_FALSE(cc::proto::deserialize("123").has_value());          // 非对象
    CHECK_FALSE(cc::proto::deserialize(R"({"type":42})").has_value());
    CHECK_FALSE(
        cc::proto::deserialize(R"({"type":"bogus"})").has_value());
    CHECK_FALSE(cc::proto::deserialize(R"({"other":1})").has_value());
}

TEST_CASE("各构造助手字段完整") {
    auto reg = cc::proto::serialize(cc::proto::makeRegister(
        "alice", "pw123", "SIGN", "DH", "SPK", "SIG"));
    CHECK(reg.find("\"username\":\"alice\"") != std::string::npos);
    CHECK(reg.find("\"type\":\"register\"") != std::string::npos);

    auto chal = cc::proto::makeLoginChallenge("NONCE", "SALT", 600000);
    CHECK(cc::proto::bodyStr(chal.body, "nonce") == "NONCE");
    CHECK(cc::proto::bodyInt(chal.body, "iterations") == 600000);

    cc::proto::PrekeyBundle bundle;
    bundle.username = "bob";
    bundle.signPub = "S";
    bundle.dhPub = "D";
    bundle.signedPrekey = "P";
    bundle.spkSig = "G";
    bundle.otp = cc::proto::OneTimePrekey{5, "O"};
    auto b = cc::proto::makeBundle(bundle);
    CHECK(b.body["otp"]["id"] == 5);
    CHECK(b.body["otp"]["pub"] == "O");

    bundle.otp.reset();
    auto b2 = cc::proto::makeBundle(bundle);
    CHECK(b2.body["otp"].is_null());

    auto users = cc::proto::makeUserList({{"alice", true}, {"bob", false}});
    REQUIRE(users.body["users"].size() == 2);
    CHECK(users.body["users"][0]["username"] == "alice");
    CHECK(users.body["users"][0]["online"] == true);
    CHECK(users.body["users"][1]["online"] == false);

    auto grp = cc::proto::makeCreateGroup("项目组", {"alice", "bob"});
    CHECK(grp.body["members"].size() == 2);
    CHECK(grp.body["name"] == "项目组");

    auto err = cc::proto::makeError("auth", "口令错误");
    CHECK(cc::proto::bodyStr(err.body, "code") == "auth");
}
