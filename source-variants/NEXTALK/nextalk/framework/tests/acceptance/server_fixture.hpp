#pragma once
#include <doctest/doctest.h>
#include "server/router/router.hpp"
#include "common/auth/login_proof.hpp"
#include <filesystem>
#include <chrono>

namespace ccfw::test {
using cc::proto::Message;
using cc::proto::MsgType;
struct Peer final : cc::server::Peer {
    std::string user;
    std::vector<Message> inbox;
    void deliver(const Message& message) override { inbox.push_back(message); }
    const std::string& username() const override { return user; }
    void setUsername(std::string name) override { user = std::move(name); }
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
    cc::Bytes passwordKey = cc::crypto::randomBytes(32);
    void save(cc::server::Storage& db, const std::string& name) const {
        REQUIRE(db.createAccount(name, passwordKey, cc::crypto::randomBytes(16), 200000,
            cc::base64Encode(signing.pub), cc::base64Encode(identity.pub), cc::base64Encode(prekey.pub),
            cc::base64Encode(cc::crypto::ed25519Sign(signing.priv, prekey.pub))).has_value());
    }
    void login(cc::server::Router& router, Peer& peer, const std::string& name, bool negotiate = true) const {
        using namespace cc::proto;
        if (negotiate) {
            peer.inbox.clear();
            router.onMessage(peer, makeHello());
            REQUIRE(peer.inbox.size() == 1);
            REQUIRE(bodyBool(peer.inbox.front().body, "ok"));
        }
        peer.inbox.clear();
        router.onMessage(peer, withV2(makeLoginRequest(name)));
        REQUIRE(peer.inbox.size() == 1);
        REQUIRE(peer.inbox.front().type == MsgType::LoginChallenge);
        cc::Bytes nonce;
        REQUIRE(cc::base64Decode(bodyStr(peer.inbox.front().body, "nonce"), nonce));
        peer.inbox.clear();
        router.onMessage(peer, withV2(makeLoginResponse(name,
            cc::base64Encode(cc::auth::deriveLoginProof(passwordKey, nonce)), cc::base64Encode(nonce),
            cc::base64Encode(cc::crypto::ed25519Sign(signing.priv, nonce)))));
        REQUIRE_FALSE(peer.inbox.empty());
        REQUIRE(peer.inbox.front().type == MsgType::LoginResult);
        REQUIRE(bodyBool(peer.inbox.front().body, "ok"));
    }
};
inline std::filesystem::path tempDirectory(std::string_view task) {
    const auto directory = std::filesystem::current_path() / (std::string(task) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    return directory;
}
} // namespace ccfw::test
