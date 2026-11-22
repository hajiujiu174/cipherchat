#pragma once
#include "ccfw/chat_service.hpp"
#include "group_flow.hpp"
#include <functional>

namespace ccfw {
class GroupService {
public:
    using Clock = std::function<std::int64_t()>;
    // 构造群聊服务，初始化后续操作需要的状态或依赖。
    GroupService(IChatService& chat, ITransport& net, AccountScope account, ILocalStore* store = nullptr, Clock clock = {});
    // 创建群聊服务。
    Result<> create(const std::string& name, const std::vector<std::string>& members);
    // 核对群 ID、成员和修订号后更新本机的群快照。
    Result<> applySnapshot(const GroupSnapshot&);
    // 同步校准群聊服务。
    Result<> reconcile(const std::vector<GroupSnapshot>&);
    // 按逻辑群消息 ID 保存逐成员投递计划，再为其他成员分别发出消息。
    Result<GroupSendReport> sendText(std::int64_t groupId, const std::string& text);
    // 从持久化群发送计划找出仍缺少投递 ID 的成员，逐项重试并保存进度。
    Result<GroupSendReport> retry(const std::string& logicalMessageId);
    // 收集当前已加载的群快照，供界面恢复群目录。
    Result<std::vector<GroupSnapshot>> snapshots();
    // 发送正在输入。
    Result<> sendTyping(const std::string& peer);
    // 清除临时状态。
    void clearTransient() noexcept { lastTyping_.clear(); }
    // 绑定账号。
    void bindAccount(AccountScope account) { account_=std::move(account); groups_.clear();drafts_.clear();restored_=false;lastTyping_.clear(); }
private:
    // 恢复群聊服务。
    Result<> restore();
    // 持久化群聊服务。
    Result<> persist(const nextalk::chat::GroupCatalog&, const std::vector<GroupDraft>&);
    IChatService& chat_;
    ITransport& net_;
    AccountScope account_;
    nextalk::chat::GroupCatalog groups_;
    std::vector<GroupDraft> drafts_;
    ILocalStore* store_;
    Clock clock_;
    bool restored_ = false;
    std::map<std::string, std::int64_t> lastTyping_;
};
} // namespace ccfw
