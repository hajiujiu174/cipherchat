#pragma once
#include "ccfw/local_store.hpp"
#include "ccfw/session_service.hpp"
#include "ccfw/transport.hpp"
#include "ccfw/direct_file_channel.hpp"

namespace ccfw {
class FileTransferService {
public:
    FileTransferService(ITransport& net, ILocalStore& store, ISessionService& sessions, DirectFileChannel* direct = nullptr);
    ~FileTransferService();
    FileTransferService(const FileTransferService&) = delete;
    FileTransferService& operator=(const FileTransferService&) = delete;
    void setDirectEnabled(bool enabled); // Business thread; disabling falls back on the next tick.
    void setCacheSupported(bool enabled);
    Result<> receiveCacheResult(const cc::proto::Message&);
    Result<> setDownloadDirectory(const std::filesystem::path&);
    Result<> tick(); // 每次至多 4 个 64KiB 读写/哈希块
    std::vector<FileProgress> takeEvents();
    Result<std::string> start(const std::string& peer, const std::filesystem::path&);
    Result<std::string> startAssigned(const std::string& peer, const std::filesystem::path&, const std::string& id,
                                     const std::string& bundleId, const std::string& itemId);
    Result<> receiveMetadata(const WireEnvelope&);
    Result<> receiveAck(const WireEnvelope&);
    Result<> receiveChunk(const WireEnvelope&);
    Result<> acknowledge(const std::string& authenticatedPeer, const std::string& fileId,
                         std::uint32_t sequence, std::uint32_t window = 16);
    Result<> resume(const std::string& fileId);
    Result<> cancel(const std::string& fileId);
    Result<FileProgress> progress(const std::string& fileId) const;
private:
    Result<> consume(const PlainEnvelope&);
    Result<> acceptMetadata(const PlainEnvelope&);
    Result<> sendAck(const std::string& fileId);
    Result<> finish(const std::string& fileId);
    Result<> processWindow(const std::string& id, bool force, std::size_t budget);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ITransport& net_;
    ILocalStore& store_;
    ISessionService& sessions_; // file_meta 通过会话加密后进入 outbox
    DirectFileChannel* direct_ = nullptr;
    bool directEnabled_ = true;
};
} // namespace ccfw
