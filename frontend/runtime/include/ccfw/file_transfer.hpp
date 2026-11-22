#pragma once
#include "ccfw/local_store.hpp"
#include "ccfw/session_service.hpp"
#include "ccfw/transport.hpp"

namespace ccfw {
class FileTransferService {
public:
    // 构造文件传输服务，初始化后续操作需要的状态或依赖。
    FileTransferService(ITransport& net, ILocalStore& store, ISessionService& sessions);
    // 销毁文件传输服务时释放它持有的资源。
    ~FileTransferService();
    // 构造文件传输服务，初始化后续操作需要的状态或依赖。
    FileTransferService(const FileTransferService&) = delete;
    // 将来源对象的状态赋值给当前对象。
    FileTransferService& operator=(const FileTransferService&) = delete;
    // 设置文件传输服务使用的缓存受支持。
    void setCacheSupported(bool enabled);
    // 接收服务端文件缓存响应，按操作类型更新上传确认或交给接收端处理数据块。
    Result<> receiveCacheResult(const cc::proto::Message&);
    // 设置文件传输服务使用的下载目录。
    Result<> setDownloadDirectory(const std::filesystem::path&);
    // 推进活跃文件任务的准备、窗口发送、缓存读写及超时重试。
    Result<> tick(); // 每次至多 4 个 64KiB 读写/哈希块
    // 取出事件。
    std::vector<FileProgress> takeEvents();
    // 启动文件传输服务。
    Result<std::string> start(const std::string& peer, const std::filesystem::path&,std::int64_t groupId=0);
    // 校验路径、收件人和固定文件 ID，持久化初始检查点及关联的交付条目。
    Result<std::string> startAssigned(const std::string& peer, const std::filesystem::path&, const std::string& id,
                                     const std::string& bundleId, const std::string& itemId,std::int64_t groupId=0);
    // 接收元数据。
    Result<> receiveMetadata(const WireEnvelope&);
    // 接收接收确认。
    Result<> receiveAck(const WireEnvelope&);
    // 核对文件块的路由、序号和窗口后加入接收缓冲，按已验证进度处理重复块。
    Result<> receiveChunk(const WireEnvelope&);
    // 确认文件传输服务。
    Result<> acknowledge(const std::string& authenticatedPeer, const std::string& fileId,
                         std::uint32_t sequence, std::uint32_t window = 16);
    // 从持久化检查点恢复文件读取或下载，并核对源文件或临时文件状态。
    Result<> resume(const std::string& fileId);
    // 为指定传输保存暂停状态，停止继续推进新的文件块。
    Result<> pause(const std::string& fileId);
    // 取消文件传输服务。
    Result<> cancel(const std::string& fileId);
    // 返回文件传输服务的当前进度。
    Result<FileProgress> progress(const std::string& fileId) const;
private:
    // 把已验证的文件载荷分派到文件元数据或签收处理。
    Result<> consume(const PlainEnvelope&);
    // 验证文件元数据和传输对端，建立接收检查点和安全的临时路径。
    Result<> acceptMetadata(const PlainEnvelope&);
    // 发送接收确认。
    Result<> sendAck(const std::string& fileId);
    // 核对整份文件摘要与长度，将临时文件转为最终文件并通知界面完成。
    Result<> finish(const std::string& fileId);
    // 以连续块为窗口校验、写入并保存新检查点，再回报已确认的文件偏移。
    Result<> processWindow(const std::string& id, bool force, std::size_t budget);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ITransport& net_;
    ILocalStore& store_;
    ISessionService& sessions_;
};
} // namespace ccfw
