#pragma once
#include "ccfw/chat_service.hpp"
#include <functional>

namespace ccfw {
class GroupService {
public:
    using Clock = std::function<std::int64_t()>;
    GroupService(IChatService& chat, ITransport& net, AccountScope account, ILocalStore* store = nullptr, Clock clock = {});
    Result<> create(const std::string& name, const std::vector<std::string>& members);
    Result<> applySnapshot(const GroupSnapshot&);
    Result<GroupSendReport> sendText(std::int64_t groupId, const std::string& text);
    Result<GroupSendReport> retry(const std::string& logicalMessageId);
    Result<std::vector<GroupSnapshot>> snapshots();
    Result<> sendTyping(const std::string& peer);
    void clearTransient() noexcept { lastTyping_.clear(); }
private:
    Result<> restore();
    Result<> persist(const std::map<std::int64_t, GroupSnapshot>&, const std::vector<GroupDraft>&);
    IChatService& chat_;
    ITransport& net_;
    AccountScope account_;
    std::map<std::int64_t, GroupSnapshot> groups_;
    std::vector<GroupDraft> drafts_;
    ILocalStore* store_;
    Clock clock_;
    bool restored_ = false;
    std::map<std::string, std::int64_t> lastTyping_;
};
} // namespace ccfw
