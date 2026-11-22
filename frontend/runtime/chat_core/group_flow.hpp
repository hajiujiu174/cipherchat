// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_GROUP_FLOW_HPP
#define NEXTALK_CHAT_GROUP_FLOW_HPP

#include "directory_store.hpp"
#include <map>

namespace nextalk {
namespace chat {

typedef std::map<int64_t, GroupRow> GroupCatalog;

enum GroupUpdate { GroupChanged, GroupInvalid, GroupConflict, GroupOutdated };
enum GroupSend { GroupReady, GroupUnavailable, GroupForbidden, GroupNoRecipients };

bool validGroup(const GroupRow& group, const std::string& account);
bool groupContains(const GroupRow& group, const std::string& name);
bool createGroupRoster(const std::string& name, const std::vector<std::string>& requested,
                       const std::string& account, std::vector<std::string>& members);
bool restoreGroupCatalog(const std::vector<GroupRow>& stored, const std::string& account,
                         GroupCatalog& output);
GroupUpdate updateGroupCatalog(GroupCatalog& catalog, GroupRow group, const std::string& account);
bool reconcileGroupCatalog(GroupCatalog& catalog, const std::vector<GroupRow>& active,
                           const std::string& account);
GroupSend planGroupSend(const GroupCatalog& groups, int64_t id, const std::string& text,
                        const std::string& account, std::vector<std::string>& recipients);

class GroupRetry {
public:
    virtual ~GroupRetry() {}
    virtual std::size_t size() const = 0;
    virtual bool sent(std::size_t index) const = 0;
    virtual bool tryAndPersist(std::size_t index) = 0;
};

// 每次尝试都先持久化。保存失败时禁止继续向后发送。
bool retryGroup(GroupRetry& plan);

} // namespace chat
} // namespace nextalk

#endif
