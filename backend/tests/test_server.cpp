#include <doctest/doctest.h>
#include <cstdio>

#include "common/crypto/crypto.hpp"
#include "common/protocol/frame.hpp"
#include "server/router/router.hpp"

#include <memory>
#include <vector>

namespace {

using namespace cc;
using namespace cc::proto;
using namespace cc::server;

// 假连接：记录所有下行消息
struct FakePeer : Peer {
    std::string user;
    std::vector<Message> outbox;

    // 投递测试。
    void deliver(const Message& msg) override { outbox.push_back(msg); }
    // 返回测试使用的用户名。
    const std::string& username() const override { return user; }
    // 设置测试使用的用户名。
    void setUsername(std::string name) override { user = std::move(name); }

    // 验证“await”场景中的关键结果，断言失败时由测试框架报告。
    const Message& await(std::size_t index = 0) const {
        REQUIRE(outbox.size() > index);
        return outbox[index];
    }
    // 检查测试是否具有类型。
    bool hasType(MsgType t) const {
        for (const auto& m : outbox) {
            if (m.type == t) return true;
        }
        return false;
    }
    // 清除测试。
    void clear() { outbox.clear(); }
};

// 一台"服务器"：内存库 + 路由
struct Server {
    Storage db{""};
    Router router{db};

    // 建立连接测试。
    std::unique_ptr<FakePeer> connect() { return std::make_unique<FakePeer>(); }
};

// 当前普通聊天账号：直接使用口令认证，不生成或持有身份私钥。
struct TestUser {
    std::string password = "口令-123456";
    // 注册Msg。
    Message registerMsg() const {
        auto message = makeRegister("", password, "", "", "", "");
        message.body["chat_mode"] = "ordinary-v1";
        return message;
    }
};
// 返回使用当前聊天协议的密码登录请求。
Message ordinaryLoginRequest(const std::string& username, const std::string& password) {
    auto message = makeLoginRequest(username, password);
    message.body["chat_mode"] = "ordinary-v1";
    return message;
}

// 验证“doRegister”场景中的关键结果，断言失败时由测试框架报告。
void doRegister(Server& s, FakePeer& peer, const std::string& name,
                const TestUser& u) {
    peer.clear();
    Message m = u.registerMsg();
    m.body["username"] = name;
    s.router.onMessage(peer, m);
    REQUIRE(peer.await(0).type == MsgType::RegisterResult);
    const bool regOk = bodyBool(peer.await(0).body, "ok");
    if (!regOk) {
        std::fprintf(stderr, "[doRegister(%s)] 失败: %s\n", name.c_str(),
                     peer.await(0).body.dump().c_str());
    }
    REQUIRE(regOk);
    peer.clear();
}

// 验证“do登录”场景中的关键结果，断言失败时由测试框架报告。
void doLogin(Server& s, FakePeer& peer, const std::string& name,
             const TestUser& u) {
    peer.clear();
    s.router.onMessage(peer, ordinaryLoginRequest(name, u.password));
    const Message& result = peer.await();
    REQUIRE(result.type == MsgType::LoginResult);
    REQUIRE(bodyBool(result.body, "ok"));
    CHECK(peer.username() == name);
    // 保留登录结果后的离线消息，供后续用例验证。
}

}  // namespace

// 验证用例“注册：成功、重名拒绝、非法输入拒绝”所描述的行为。
TEST_CASE("注册：成功、重名拒绝、非法输入拒绝") {
    Server s;
    auto peer = s.connect();
    TestUser alice;

    doRegister(s, *peer, "alice", alice);
    // 重名
    TestUser alice2;
    s.router.onMessage(*peer, [&] {
        Message m = alice2.registerMsg();
        m.body["username"] = "alice";
        return m;
    }());
    CHECK(peer->await(0).type == MsgType::RegisterResult);
    CHECK_FALSE(bodyBool(peer->await(0).body, "ok"));
    // 弱口令
    s.router.onMessage(*peer, makeRegister("bob", "123", "S", "D", "P", "G"));
    CHECK_FALSE(bodyBool(peer->await(1).body, "ok"));
    // 未声明当前聊天模式的旧客户端必须拒绝
    s.router.onMessage(*peer, makeRegister("bob", "123456", "!!", "D", "P", "G"));
    CHECK_FALSE(bodyBool(peer->await(2).body, "ok"));
    // 合法用户名含中文
    TestUser zhang;
    doRegister(s, *peer, "张三", zhang);
}

// 验证原账号密码、错误密码和无效字段。
TEST_CASE("登录：一次请求校验已存密码，错误凭据不能绑定连接") {
    Server s;
    auto peer = s.connect();
    TestUser alice;
    doRegister(s, *peer, "alice", alice);

    auto request = ordinaryLoginRequest("alice", alice.password);
    SUBCASE("right password") {
        doLogin(s, *peer, "alice", alice);
        CHECK(s.router.isOnline("alice"));
        CHECK(peer->outbox.size() == 1);
    }
    SUBCASE("invalid credentials") {
        SUBCASE("wrong password") { request.body["password"] = "wrong-password"; }
        SUBCASE("unknown user") { request.body["username"] = "unknown"; }
        SUBCASE("invalid password type") { request.body["password"] = 123456; }
        SUBCASE("oversized password") { request.body["password"] = std::string(1025, 'a'); }
        s.router.onMessage(*peer, request);
        REQUIRE(peer->outbox.size() == 1);
        CHECK(peer->await().type == MsgType::LoginResult);
        CHECK_FALSE(bodyBool(peer->await().body, "ok"));
        CHECK(peer->username().empty());
        CHECK_FALSE(s.router.isOnline("alice"));
    }
}

// 验证用例“presence：上线广播给已在线用户；下线广播给剩余用户”所描述的行为。
TEST_CASE("presence：上线广播给已在线用户；下线广播给剩余用户") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);

    // alice 上线时 bob 离线 → 无人收到广播
    doLogin(s, *a, "alice", ua);
    CHECK_FALSE(b->hasType(MsgType::Presence));
    CHECK(a->outbox.size() == 1);  // 仅返回登录结果

    // bob 上线 → 已在线的 alice 收到 presence(bob)
    doLogin(s, *b, "bob", ub);
    CHECK(b->outbox.size() == 1);  // 新登录者不收到旧用户的存在广播
    const Message& p = a->await(1);
    REQUIRE(p.type == MsgType::Presence);
    CHECK(bodyStr(p.body, "username") == "bob");
    CHECK(bodyBool(p.body, "online"));

    // alice 断开 → bob 收到离线广播
    s.router.onDisconnect(*a);
    const Message& off = b->await(1);
    REQUIRE(off.type == MsgType::Presence);
    CHECK(bodyStr(off.body, "username") == "alice");
    CHECK_FALSE(bodyBool(off.body, "online"));
    CHECK_FALSE(s.router.isOnline("alice"));
}

// 验证用例“消息中转：在线直投+ACK；离线入队+登录补投”所描述的行为。
TEST_CASE("消息中转：在线直投+ACK；离线入队+登录补投") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);

    // bob 离线时 alice 发消息 → 入离线队列，ACK delivered=false
    doLogin(s, *a, "alice", ua);  // a: [result]
    s.router.onMessage(*a, makeSendMsg("m-1", "bob", "chat", "RU5DX0NJVFIG"));
    const Message& ack1 = a->await(1);
    REQUIRE(ack1.type == MsgType::MsgAck);
    CHECK(ack1.msgId == "m-1");
    CHECK_FALSE(bodyBool(ack1.body, "delivered"));

    // 目标用户不存在 → error
    s.router.onMessage(*a, makeSendMsg("m-2", "nobody", "chat", "eHg="));
    CHECK(a->await(2).type == MsgType::Error);

    // bob 上线 → 收到补投的 recv_msg（b: [result, recv]）
    doLogin(s, *b, "bob", ub);
    const Message& delivered = b->await(1);
    REQUIRE(delivered.type == MsgType::RecvMsg);
    CHECK(delivered.msgId == "m-1");
    CHECK(bodyStr(delivered.body, "from") == "alice");
    CHECK(bodyStr(delivered.body, "kind") == "chat");
    CHECK(bodyStr(delivered.body, "payload") == "RU5DX0NJVFIG");
    CHECK(bodyInt(delivered.body, "ts") > 0);

    // 双方在线：直投 + delivered=true
    // （bob 登录时 alice 还会收到 presence(bob)，位于下标 3）
    s.router.onMessage(*a, makeSendMsg("m-3", "bob", "chat", "bmV3"));
    const Message& ack3 = a->await(4);
    REQUIRE(ack3.type == MsgType::MsgAck);
    CHECK(bodyBool(ack3.body, "delivered"));
    const Message& recv3 = b->await(2);
    REQUIRE(recv3.type == MsgType::RecvMsg);
    CHECK(recv3.msgId == "m-3");
}

// 验证用例“普通聊天拒绝已停用的预共享密钥与公钥束接口”所描述的行为。
TEST_CASE("普通聊天拒绝已停用的预共享密钥与公钥束接口") {
    Server s;
    auto peer = s.connect();
    TestUser user;
    doRegister(s, *peer, "alice", user);
    const auto account = s.db.findAccount("alice");
    REQUIRE(account);
    CHECK(account->signPub.empty());
    CHECK(account->dhPub.empty());
    CHECK(account->signedPrekey.empty());
    CHECK(account->spkSig.empty());
    doLogin(s, *peer, "alice", user);
    peer->clear();
    s.router.onMessage(*peer, makeUploadPrekeys({{11, base64Encode(crypto::randomBytes(32))}}));
    REQUIRE(peer->await(0).type == MsgType::Error);
    CHECK(bodyStr(peer->await(0).body, "code") == "unsupported");
    s.router.onMessage(*peer, makeFetchBundle("alice"));
    REQUIRE(peer->await(1).type == MsgType::Error);
    CHECK(bodyStr(peer->await(1).body, "code") == "unsupported");
    CHECK(s.db.findAccount("alice")->signedPrekey.empty());
}

// 验证用例“旧登录协议拒绝且不创建可重放的登录挑战”所描述的行为。
TEST_CASE("旧登录协议拒绝且不创建可重放的登录挑战") {
    Server s;
    auto peer = s.connect();
    TestUser user;
    doRegister(s, *peer, "alice", user);
    s.router.onMessage(*peer, makeLoginRequest("alice"));
    REQUIRE(peer->await(0).type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(peer->await(0).body, "ok"));
    peer->clear();
    auto obsolete = makeLoginRequest("alice");
    obsolete.body["chat_mode"] = "ordinary-v1";
    s.router.onMessage(*peer, obsolete);
    REQUIRE(peer->await().type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(peer->await().body, "ok"));
    peer->clear();
    s.router.onMessage(*peer, makeLoginResponse("alice", "", "", ""));
    CHECK(peer->await().type == MsgType::Error);
    CHECK_FALSE(s.router.isOnline("alice"));
}

// 验证用例“typing 与 file_ack 透传到目标”所描述的行为。
TEST_CASE("typing 与 file_ack 透传到目标") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);
    doLogin(s, *a, "alice", ua);
    doLogin(s, *b, "bob", ub);
    a->clear();
    b->clear();

    s.router.onMessage(*a, makeTypingTo("bob"));
    const Message& t = b->await(0);
    REQUIRE(t.type == MsgType::Typing);
    CHECK(bodyStr(t.body, "from") == "alice");

    s.router.onMessage(*b, [&] {
        Message m = makeFileAck("m-1", 3);
        m.body["to"] = "alice";
        return m;
    }());
    const Message& fa = a->await(0);
    REQUIRE(fa.type == MsgType::FileAck);
    CHECK(fa.msgId == "m-1");
    CHECK(bodyInt(fa.body, "seq") == 3);
}

// 验证用例“群组：创建成功通知成员；成员不存在拒绝”所描述的行为。
TEST_CASE("群组：创建成功通知成员；成员不存在拒绝") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);
    doLogin(s, *a, "alice", ua);
    doLogin(s, *b, "bob", ub);
    a->clear();
    b->clear();

    s.router.onMessage(*a, makeCreateGroup("项目组", {"bob"}));
    const Message& gr = a->await(0);
    REQUIRE(gr.type == MsgType::GroupResult);
    REQUIRE(bodyBool(gr.body, "ok"));
    const std::int64_t gid = bodyInt(gr.body, "group_id");
    CHECK(gid > 0);
    // alice 被自动加入
    bool hasAlice = false, hasBob = false;
    for (const auto& m : gr.body["members"]) {
        if (m == "alice") hasAlice = true;
        if (m == "bob") hasBob = true;
    }
    CHECK(hasAlice);
    CHECK(hasBob);
    // bob 在线 → 收到 group_update
    const Message& gu = b->await(0);
    REQUIRE(gu.type == MsgType::GroupUpdate);
    CHECK(bodyInt(gu.body, "group_id") == gid);

    // 不存在的成员
    s.router.onMessage(*a, makeCreateGroup("坏群", {"ghost"}));
    CHECK_FALSE(bodyBool(a->await(1).body, "ok"));
    // 群组落库可查
    auto g = s.db.findGroup(gid);
    REQUIRE(g.has_value());
    CHECK(g->name == "项目组");
    CHECK(g->members.size() == 2);
}

// 验证用例“list_users 返回全量与在线状态”所描述的行为。
TEST_CASE("list_users 返回全量与在线状态") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);
    doLogin(s, *a, "alice", ua);
    doLogin(s, *b, "bob", ub);
    a->clear();

    s.router.onMessage(*a, makeListUsers());
    const Message& ul = a->await(0);
    REQUIRE(ul.type == MsgType::UserList);
    REQUIRE(ul.body["users"].size() == 2);
    // 按 id 顺序：alice 先注册
    CHECK(bodyStr(ul.body["users"][0], "username") == "alice");
    CHECK(bodyBool(ul.body["users"][0], "online"));
    CHECK(bodyStr(ul.body["users"][1], "username") == "bob");
    CHECK(bodyBool(ul.body["users"][1], "online"));
}

// 验证用例“顶号：同名二次登录踢掉旧连接”所描述的行为。
TEST_CASE("顶号：同名二次登录踢掉旧连接") {
    Server s;
    auto a1 = s.connect();
    auto a2 = s.connect();
    TestUser ua;
    doRegister(s, *a1, "alice", ua);

    doLogin(s, *a1, "alice", ua);
    doLogin(s, *a2, "alice", ua);  // 第二个连接同账号登录
    // 旧连接收到 kicked 错误
    bool kicked = false;
    for (const auto& m : a1->outbox) {
        if (m.type == MsgType::Error && bodyStr(m.body, "code") == "kicked") {
            kicked = true;
        }
    }
    CHECK(kicked);
    CHECK(s.router.isOnline("alice"));
    // online_ 只有一份映射
    CHECK(s.router.onlineUsers().size() == 1);
}
