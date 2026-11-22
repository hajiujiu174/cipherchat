#pragma once
// 服务端接口不接收 IdentityState/ClientSnapshot/ChatRecord，只有账号元数据和密文。
#include "ccfw/result.hpp"
#include "common/base64.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ccfw::server {
struct PendingDelivery {
    std::string recipient;
    std::string sender;
    std::string messageId;
    std::string ciphertextJson; // 不含明文、私钥、文件密钥
};
class IDeliveryQueue {
public:
    // 销毁server_services 模块时释放它持有的资源。
    virtual ~IDeliveryQueue() = default;
    // 加入队列server_services 模块。
    virtual Result<> enqueue(const PendingDelivery&) = 0; // 按 recipient+messageId 幂等
    // 查看但不移除server_services 模块。
    virtual Result<std::vector<PendingDelivery>> peek(const std::string& recipient,
                                                     std::size_t limit) = 0;
    // 仅接收方认证后的 ACK 才删除；socket write 成功不算签收。
    virtual Result<> acknowledge(const std::string& authenticatedRecipient,
                                 const std::string& messageId) = 0;
};
class SqliteDeliveryQueue final : public IDeliveryQueue {
public:
    // 构造服务端待投递队列，初始化后续操作需要的状态或依赖。
    SqliteDeliveryQueue();
    // 销毁服务端待投递队列时释放它持有的资源。
    ~SqliteDeliveryQueue() override;
    // 打开服务端待投递队列。
    Result<> open(const std::string& databasePath);
    // 用接收账号与消息 ID 检查待投递项或既有签收，再保存新投递。
    Result<> enqueue(const PendingDelivery&) override;
    // 按接收账号读取一批待发消息，保留原记录直至真正签收。
    Result<std::vector<PendingDelivery>> peek(const std::string&, std::size_t) override;
    // 同一事务内记录接收签收和摘要，并移除对应待投递记录。
    Result<> acknowledge(const std::string&, const std::string&) override;
    // 查找服务端待投递队列。
    Result<std::optional<PendingDelivery>> find(const std::string& recipient,
                                               const std::string& messageId);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class IGroupDirectory {
public:
    // 销毁server_services 模块时释放它持有的资源。
    virtual ~IGroupDirectory() = default;
    // 返回server_services 模块使用的成员。
    virtual Result<std::vector<std::string>> members(std::int64_t groupId) = 0;
};
class GroupAuthorizer {
public:
    // 构造server_services 模块，初始化后续操作需要的状态或依赖。
    explicit GroupAuthorizer(IGroupDirectory& groups) : groups_(groups) {}
    // 根据服务端的当前群成员表核对发送者和收件人资格。
    Result<> authorize(std::int64_t groupId, const std::string& authenticatedSender,
                       const std::string& recipient);
private:
    IGroupDirectory& groups_;
};
} // namespace ccfw::server
