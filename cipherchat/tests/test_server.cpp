#include <doctest/doctest.h>
#include <cstdio>

#include "common/crypto/crypto.hpp"
#include "common/protocol/frame.hpp"
#include "server/auth/auth.hpp"
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

    void deliver(const Message& msg) override { outbox.push_back(msg); }
    const std::string& username() const override { return user; }
    void setUsername(std::string name) override { user = std::move(name); }

    const Message& await(std::size_t index = 0) const {
        REQUIRE(outbox.size() > index);
        return outbox[index];
    }
    bool hasType(MsgType t) const {
        for (const auto& m : outbox) {
            if (m.type == t) return true;
        }
        return false;
    }
    void clear() { outbox.clear(); }
};

// 一台"服务器"：内存库 + 路由
struct Server {
    Storage db{""};
    Router router{db};

    std::unique_ptr<FakePeer> connect() { return std::make_unique<FakePeer>(); }
};

// 测试用户的密码学身份
struct TestUser {
    cc::crypto::X25519KeyPair ik;
    cc::crypto::Ed25519KeyPair sign;
    cc::crypto::X25519KeyPair spk;
    cc::crypto::X25519KeyPair opk;
    Bytes spkSig;
    std::string password = "口令-123456";

    TestUser() {
        ik = cc::crypto::generateX25519();
        sign = cc::crypto::generateEd25519();
        spk = cc::crypto::generateX25519();
        opk = cc::crypto::generateX25519();
        spkSig = cc::crypto::ed25519Sign(sign.priv, spk.pub);
    }

    Message registerMsg() const {
        return makeRegister("", password, base64Encode(sign.pub),
                            base64Encode(ik.pub), base64Encode(spk.pub),
                            base64Encode(spkSig));
    }

    // 按协议计算登录证明与挑战签名
    std::pair<Bytes, Bytes> loginProof(const Bytes& nonce, const Bytes& salt,
                                       std::int64_t iterations) const {
        const Bytes k = cc::crypto::pbkdf2Sha256(password, salt, iterations, 32);
        return {deriveLoginProof(k, nonce),
                cc::crypto::ed25519Sign(sign.priv, nonce)};
    }
};

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

void doLogin(Server& s, FakePeer& peer, const std::string& name,
             const TestUser& u) {
    peer.clear();
    s.router.onMessage(peer, makeLoginRequest(name));
    const Message chal = peer.await(0);
    REQUIRE(chal.type == MsgType::LoginChallenge);
    Bytes nonce, salt;
    REQUIRE(base64Decode(bodyStr(chal.body, "nonce"), nonce));
    REQUIRE(base64Decode(bodyStr(chal.body, "salt"), salt));
    const auto [proof, sig] =
        u.loginProof(nonce, salt, bodyInt(chal.body, "iterations"));
    s.router.onMessage(peer, makeLoginResponse(name, base64Encode(proof),
                                               base64Encode(nonce),
                                               base64Encode(sig)));
    const Message& result = peer.await(1);
    REQUIRE(result.type == MsgType::LoginResult);
    REQUIRE(bodyBool(result.body, "ok"));
    // 保留 outbox：登录期间服务器可能已推送 presence / 离线消息，
    // 位于下标 2 起。不关心推送的用例自行 clear()。
    peer.user = name;
}

}  // namespace

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
    // 非法公钥材料
    s.router.onMessage(*peer, makeRegister("bob", "123456", "!!", "D", "P", "G"));
    CHECK_FALSE(bodyBool(peer->await(2).body, "ok"));
    // 合法用户名含中文
    TestUser zhang;
    doRegister(s, *peer, "张三", zhang);
}

TEST_CASE("登录：正确口令成功；错口令/坏签名/重放 nonce 失败") {
    Server s;
    auto peer = s.connect();
    TestUser alice;
    doRegister(s, *peer, "alice", alice);

    // 正常登录
    doLogin(s, *peer, "alice", alice);
    CHECK(s.router.isOnline("alice"));

    // 错误口令
    peer->clear();
    TestUser evil;
    evil.password = "wrong-password";
    s.router.onMessage(*peer, makeLoginRequest("alice"));
    const Message chal = peer->await(0);
    Bytes nonce, salt;
    REQUIRE(base64Decode(bodyStr(chal.body, "nonce"), nonce));
    REQUIRE(base64Decode(bodyStr(chal.body, "salt"), salt));
    {
        const auto [proof, sig] =
            evil.loginProof(nonce, salt, bodyInt(chal.body, "iterations"));
        s.router.onMessage(*peer, makeLoginResponse("alice", base64Encode(proof),
                                                    base64Encode(nonce),
                                                    base64Encode(sig)));
    }
    const Message& r1 = peer->await(1);
    CHECK(r1.type == MsgType::LoginResult);
    CHECK_FALSE(bodyBool(r1.body, "ok"));

    // 正确口令但签名来自他人（没有身份私钥）→ 拒绝
    peer->clear();
    TestUser impostor;
    s.router.onMessage(*peer, makeLoginRequest("alice"));
    const Message chal2 = peer->await(0);
    Bytes nonce2, salt2;
    REQUIRE(base64Decode(bodyStr(chal2.body, "nonce"), nonce2));
    REQUIRE(base64Decode(bodyStr(chal2.body, "salt"), salt2));
    {
        const auto [proof, _] =
            alice.loginProof(nonce2, salt2, bodyInt(chal2.body, "iterations"));
        (void)_;
        s.router.onMessage(*peer,
                           makeLoginResponse("alice", base64Encode(proof),
                                             base64Encode(nonce2),
                                             base64Encode(
                                                 cc::crypto::ed25519Sign(
                                                     impostor.sign.priv,
                                                     nonce2))));
    }
    const Message& r2 = peer->await(1);
    CHECK_FALSE(bodyBool(r2.body, "ok"));

    // 正常登录后断开，重放旧 nonce → 拒绝（nonce 一次性）
    FakePeer replay;
    doLogin(s, replay, "alice", alice);
    replay.clear();
    s.router.onDisconnect(replay);
    const auto [proof3, sig3] =
        alice.loginProof(nonce2, salt2, bodyInt(chal2.body, "iterations"));
    s.router.onMessage(replay, makeLoginResponse("alice", base64Encode(proof3),
                                                 base64Encode(nonce2),
                                                 base64Encode(sig3)));
    const Message& r3 = replay.await(0);
    CHECK_FALSE(bodyBool(r3.body, "ok"));
}

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
    CHECK(a->outbox.size() == 2);  // 仅 challenge+result

    // bob 上线 → 已在线的 alice 收到 presence(bob)
    doLogin(s, *b, "bob", ub);
    CHECK(b->outbox.size() == 2);  // 新登录者不收到旧用户的存在广播
    const Message& p = a->await(2);
    REQUIRE(p.type == MsgType::Presence);
    CHECK(bodyStr(p.body, "username") == "bob");
    CHECK(bodyBool(p.body, "online"));

    // alice 断开 → bob 收到离线广播
    s.router.onDisconnect(*a);
    const Message& off = b->await(2);
    REQUIRE(off.type == MsgType::Presence);
    CHECK(bodyStr(off.body, "username") == "alice");
    CHECK_FALSE(bodyBool(off.body, "online"));
    CHECK_FALSE(s.router.isOnline("alice"));
}

TEST_CASE("消息中转：在线直投+ACK；离线入队+登录补投") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);

    // bob 离线时 alice 发消息 → 入离线队列，ACK delivered=false
    doLogin(s, *a, "alice", ua);  // a: [challenge, result]
    s.router.onMessage(*a, makeSendMsg("m-1", "bob", "chat", "RU5DX0NJVFIG"));
    const Message& ack1 = a->await(2);
    REQUIRE(ack1.type == MsgType::MsgAck);
    CHECK(ack1.msgId == "m-1");
    CHECK_FALSE(bodyBool(ack1.body, "delivered"));

    // 目标用户不存在 → error
    s.router.onMessage(*a, makeSendMsg("m-2", "nobody", "chat", "eHg="));
    CHECK(a->await(3).type == MsgType::Error);

    // bob 上线 → 收到补投的 recv_msg（b: [challenge, result, recv]）
    doLogin(s, *b, "bob", ub);
    const Message& delivered = b->await(2);
    REQUIRE(delivered.type == MsgType::RecvMsg);
    CHECK(delivered.msgId == "m-1");
    CHECK(bodyStr(delivered.body, "from") == "alice");
    CHECK(bodyStr(delivered.body, "kind") == "chat");
    CHECK(bodyStr(delivered.body, "payload") == "RU5DX0NJVFIG");
    CHECK(bodyInt(delivered.body, "ts") > 0);

    // 双方在线：直投 + delivered=true
    // （bob 登录时 alice 还会收到 presence(bob)，位于下标 4）
    s.router.onMessage(*a, makeSendMsg("m-3", "bob", "chat", "bmV3"));
    const Message& ack3 = a->await(5);
    REQUIRE(ack3.type == MsgType::MsgAck);
    CHECK(bodyBool(ack3.body, "delivered"));
    const Message& recv3 = b->await(3);
    REQUIRE(recv3.type == MsgType::RecvMsg);
    CHECK(recv3.msgId == "m-3");
}

TEST_CASE("prekey：上传后按 FIFO 弹出，耗尽后 bundle 的 otp 为空") {
    Server s;
    auto a = s.connect();
    auto b = s.connect();
    TestUser ua, ub;
    doRegister(s, *a, "alice", ua);
    doRegister(s, *b, "bob", ub);
    doLogin(s, *b, "bob", ub);  // b: [challenge, result]

    // bob 上传 2 条一次性密钥
    s.router.onMessage(*b, makeUploadPrekeys({{11, base64Encode(ub.opk.pub)},
                                              {12, base64Encode(ub.opk.pub)}}));
    const Message& up = b->await(2);
    REQUIRE(up.type == MsgType::UploadPrekeysResult);
    CHECK(bodyBool(up.body, "ok"));
    CHECK(bodyInt(up.body, "count") == 2);

    doLogin(s, *a, "alice", ua);  // a: [challenge, result]
    s.router.onMessage(*a, makeFetchBundle("bob"));
    const Message& b1 = a->await(2);
    REQUIRE(b1.type == MsgType::Bundle);
    CHECK(bodyStr(b1.body, "username") == "bob");
    CHECK(bodyStr(b1.body, "dh_pub") == base64Encode(ub.ik.pub));
    REQUIRE(b1.body["otp"].is_object());
    CHECK(b1.body["otp"]["id"] == 11);

    s.router.onMessage(*a, makeFetchBundle("bob"));
    CHECK(a->await(3).body["otp"]["id"] == 12);

    s.router.onMessage(*a, makeFetchBundle("bob"));
    CHECK(a->await(4).body["otp"].is_null());

    // 未登录拉取 → error
    auto anon = s.connect();
    s.router.onMessage(*anon, makeFetchBundle("bob"));
    CHECK(anon->await(0).type == MsgType::Error);
}

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
