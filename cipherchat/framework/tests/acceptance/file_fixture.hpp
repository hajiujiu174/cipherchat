#pragma once
#include "client_fixture.hpp"
#include "../../src/wire_helpers.hpp"
#include "client/core/files.hpp"
#include <memory>

namespace ccfw::test {
struct CaptureTransport final : ITransport {
    std::vector<cc::proto::Message> sent;
    bool connected = true;
    Result<> connect(const ConnectionOptions&) override { connected = true; return Result<>::success({}); }
    Result<> send(const cc::proto::Message& m) override {
        if (!connected) return Result<>::failure({ErrorCode::NotConnected, "test", "offline"});
        sent.push_back(m); return Result<>::success({});
    }
    Result<std::optional<cc::proto::Message>> poll() override { return Result<std::optional<cc::proto::Message>>::success(std::nullopt); }
    void close() noexcept override { connected = false; }
};
struct FileClient {
    std::filesystem::path directory;
    PlatformSecretProtector protector;
    SqliteLocalStore disk{protector}; ToggleStore store{disk}; CaptureTransport net;
    SessionService sessions{store}; ChatService chat{net, sessions, store}; std::unique_ptr<DirectFileChannel> direct; FileTransferService files;
    FileClient(std::string name, std::filesystem::path path = {}, bool hybrid = false)
        : directory(path.empty() ? tempDirectory(name) : path), direct(hybrid ? std::make_unique<DirectFileChannel>() : nullptr), files(net, store, sessions, direct.get()) {
        REQUIRE(store.open(directory / "client.db", {"file-test", name}));
        auto snapshot = store.load().value();
        if (!snapshot.identity) {
            IdentityState identity; identity.signing = cc::crypto::generateEd25519(); identity.identityDh = cc::crypto::generateX25519();
            identity.signedPrekey = cc::crypto::generateX25519(); identity.signedPrekeySignature = cc::crypto::ed25519Sign(identity.signing.priv, identity.signedPrekey.pub);
            snapshot.identity = identity; const auto revision = snapshot.revision++; REQUIRE(store.commit(revision, snapshot));
        }
        REQUIRE(files.setDownloadDirectory(directory / "downloads"));
    }
    void trust(FileClient& peer) {
        auto snapshot = peer.store.load().value(); auto identity = *snapshot.identity;
        REQUIRE(sessions.acceptPeerIdentity({snapshot.account.username, identity.signing.pub, identity.identityDh.pub, true}));
    }
    cc::proto::PrekeyBundle bundle() {
        const auto snapshot = store.load().value(); const auto i = *snapshot.identity;
        return {snapshot.account.username, cc::base64Encode(i.signing.pub), cc::base64Encode(i.identityDh.pub),
            cc::base64Encode(i.signedPrekey.pub), cc::base64Encode(i.signedPrekeySignature), std::nullopt};
    }
};
struct FilePackets { std::string id; cc::Bytes key; WireEnvelope metadata; std::vector<WireEnvelope> chunks; };
inline FilePackets packets(FileClient& sender, FileClient& receiver, cc::BytesView data, const std::string& name = "source.bin") {
    FilePackets result; result.id = wire::id(); result.key = cc::crypto::randomBytes(32);
    auto a = sender.store.load().value(), b = receiver.store.load().value();
    REQUIRE(sender.sessions.establish(b.account.username, wire::bundle(receiver.bundle())));
    const auto count = static_cast<std::uint32_t>((data.size() + 65535) / 65536);
    const nlohmann::json meta = {{"version", 2}, {"file_id", result.id}, {"name", name}, {"size_bytes", data.size()},
        {"chunk_size", 65536}, {"chunk_count", count}, {"key_base64", cc::base64Encode(result.key)}, {"sha256", cc::base64Encode(cc::crypto::sha256(data))},
        {"file_transport", "window-cbor-v1"}, {"window", 16}};
    auto encrypted = sender.sessions.encryptAndCommit({"fmeta:" + result.id, "", a.account.username, b.account.username, "file_meta", meta.dump(), 0, 1});
    REQUIRE(encrypted); result.metadata = encrypted.value();
    cc::Bytes rawId, previous; REQUIRE(cc::base64Decode(result.id, rawId));
    for (std::uint32_t seq = 0; seq < count; ++seq) {
        const auto block = data.substr(std::size_t(seq) * 65536, 65536);
        auto sealed = cc::files::sealChunk(result.key, rawId, seq, previous, block); previous = sealed.tag;
        const nlohmann::json body = {{"version", 2}, {"file_id", result.id}, {"seq", seq},
            {"cipher", codec::binary(sealed.cipher)}, {"tag", codec::binary(sealed.tag)}};
        const auto cbor = nlohmann::json::to_cbor(body); cc::Bytes payload("CCF3"); payload.append(reinterpret_cast<const char*>(cbor.data()), cbor.size());
        result.chunks.push_back({"fchunk:" + result.id + ":" + std::to_string(seq), a.account.username, b.account.username, "file_chunk", cc::base64Encode(payload), 0, 1});
    }
    return result;
}
inline cc::Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary); REQUIRE(input.good()); return {std::istreambuf_iterator<char>(input), {}};
}
inline void writeBytes(const std::filesystem::path& path, cc::BytesView data) {
    std::ofstream out(path, std::ios::binary); REQUIRE(out.good()); REQUIRE(static_cast<bool>(out.write(data.data(), static_cast<std::streamsize>(data.size()))));
}
}
