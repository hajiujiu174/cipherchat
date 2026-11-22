#pragma once
#include "client_fixture.hpp"
#include "../../src/wire_helpers.hpp"
#include <memory>

namespace ccfw::test {
struct CaptureTransport final : ITransport {
    std::vector<cc::proto::Message> sent;
    bool connected = true;
    // 建立连接测试。
    Result<> connect(const ConnectionOptions&) override { connected = true; return Result<>::success({}); }
    // 发送测试。
    Result<> send(const cc::proto::Message& m) override {
        if (!connected) return Result<>::failure({ErrorCode::NotConnected, "test", "offline"});
        sent.push_back(m); return Result<>::success({});
    }
    // 轮询测试。
    Result<std::optional<cc::proto::Message>> poll() override { return Result<std::optional<cc::proto::Message>>::success(std::nullopt); }
    // 关闭测试。
    void close() noexcept override { connected = false; }
};
struct FileClient {
    std::filesystem::path directory;
    PlatformSecretProtector protector;
    SqliteLocalStore disk{protector}; ToggleStore store{disk}; CaptureTransport net;
    SessionService sessions{store}; ChatService chat{net, sessions, store}; FileTransferService files{net, store, sessions};
    // 构造测试，初始化后续操作需要的状态或依赖。
    FileClient(std::string name, std::filesystem::path path = {})
        : directory(path.empty() ? tempDirectory(name) : path) {
        REQUIRE(store.open(directory / "client.db", {"file-test", name}));
        REQUIRE(files.setDownloadDirectory(directory / "downloads"));
    }

};
struct FilePackets { std::string id; WireEnvelope metadata; std::vector<WireEnvelope> chunks; };
// 验证“packets”场景中的关键结果，断言失败时由测试框架报告。
inline FilePackets packets(FileClient& sender, FileClient& receiver, cc::BytesView data, const std::string& name = "source.bin") {
    FilePackets result; result.id = wire::id();
    auto a = sender.store.load().value(), b = receiver.store.load().value();
    const auto count = static_cast<std::uint32_t>((data.size() + 65535) / 65536);
    const nlohmann::json meta = {{"version", 2}, {"file_id", result.id}, {"name", name}, {"size_bytes", data.size()},
        {"chunk_size", 65536}, {"chunk_count", count}, {"sha256", cc::base64Encode(cc::crypto::sha256(data))},
        {"file_transport", "plain-window-v1"}, {"window", 16}};
    auto encrypted = sender.sessions.encryptAndCommit({"fmeta:" + result.id, "", a.account.username, b.account.username, "file_meta", meta.dump(), 0, 1});
    REQUIRE(encrypted); result.metadata = encrypted.value();
    for (std::uint32_t seq = 0; seq < count; ++seq) {
        const auto block = data.substr(std::size_t(seq) * 65536, 65536);
        const nlohmann::json body = {{"version", 2}, {"file_id", result.id}, {"seq", seq},
            {"data", codec::binary(cc::Bytes(block))}, {"checksum", codec::binary(cc::crypto::sha256(block).substr(0, 16))}};
        const auto cbor = nlohmann::json::to_cbor(body); cc::Bytes payload("CCP4"); payload.append(reinterpret_cast<const char*>(cbor.data()), cbor.size());
        result.chunks.push_back({"fchunk:" + result.id + ":" + std::to_string(seq), a.account.username, b.account.username, "file_chunk", cc::base64Encode(payload), 0, 1});
    }
    return result;
}
// 验证“已读字节”场景中的关键结果，断言失败时由测试框架报告。
inline cc::Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary); REQUIRE(input.good()); return {std::istreambuf_iterator<char>(input), {}};
}
// 验证“write字节”场景中的关键结果，断言失败时由测试框架报告。
inline void writeBytes(const std::filesystem::path& path, cc::BytesView data) {
    std::ofstream out(path, std::ios::binary); REQUIRE(out.good()); REQUIRE(static_cast<bool>(out.write(data.data(), static_cast<std::streamsize>(data.size()))));
}
}
