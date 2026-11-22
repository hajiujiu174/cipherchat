#pragma once
#include <doctest/doctest.h>
#include "server/router/router.hpp"
#include "common/auth/password.hpp"
#include "common/crypto/crypto.hpp"
#include <filesystem>
#include <chrono>

namespace ccfw::test {
using cc::proto::Message;
using cc::proto::MsgType;
struct Peer final : cc::server::Peer {
    std::string user;
    std::vector<Message> inbox;
    // 投递服务端连接接口。
    void deliver(const Message& message) override { inbox.push_back(message); }
    // 返回服务端连接接口使用的用户名。
    const std::string& username() const override { return user; }
    // 设置服务端连接接口使用的用户名。
    void setUsername(std::string name) override { user = std::move(name); }
    // 取出服务端连接接口。
    std::vector<Message> take(MsgType type) {
        std::vector<Message> result;
        for (const auto& message : inbox) if (message.type == type) result.push_back(message);
        inbox.clear();
        return result;
    }
};
struct User {
    cc::crypto::Ed25519KeyPair signing = cc::crypto::generateEd25519();
    cc::crypto::X25519KeyPair identity = cc::crypto::generateX25519();
    cc::crypto::X25519KeyPair prekey = cc::crypto::generateX25519();
    std::string password = "test-password";
    // 验证“save”场景中的关键结果，断言失败时由测试框架报告。
    void save(cc::server::Storage& db, const std::string& name) const {
        const auto salt = cc::crypto::randomBytes(cc::auth::kPbkdf2SaltLen);
        const auto hash = cc::crypto::pbkdf2Sha256(password, salt, cc::auth::kPbkdf2Iterations, cc::auth::kPbkdf2KeyLen);
        REQUIRE(db.createAccount(name, hash, salt, cc::auth::kPbkdf2Iterations,
            cc::base64Encode(signing.pub), cc::base64Encode(identity.pub), cc::base64Encode(prekey.pub),
            cc::base64Encode(cc::crypto::ed25519Sign(signing.priv, prekey.pub))).has_value());
    }
    // 验证“登录”场景中的关键结果，断言失败时由测试框架报告。
    void login(cc::server::Router& router, Peer& peer, const std::string& name, bool negotiate = true) const {
        using namespace cc::proto;
        if (negotiate) {
            peer.inbox.clear();
            router.onMessage(peer, makeHello());
            REQUIRE(peer.inbox.size() == 1);
            REQUIRE(bodyBool(peer.inbox.front().body, "ok"));
        }
        peer.inbox.clear();
        auto request = withV2(makeLoginRequest(name, password));
        request.body["chat_mode"] = "ordinary-v1";
        router.onMessage(peer, request);
        REQUIRE_FALSE(peer.inbox.empty());
        REQUIRE(peer.inbox.front().type == MsgType::LoginResult);
        REQUIRE(bodyBool(peer.inbox.front().body, "ok"));
    }
};
// 返回测试使用的temp目录。
inline std::filesystem::path tempDirectory(std::string_view task) {
    const auto directory = std::filesystem::current_path() / (std::string(task) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    return directory;
}
} // namespace ccfw::test
