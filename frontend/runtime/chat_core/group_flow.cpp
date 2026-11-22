// 人工实现，由 ChatGPT 规范化。

#include "group_flow.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace nextalk {
namespace chat {

bool groupContains(const GroupRow& group, const std::string& name) {
    return std::find(group.members.begin(), group.members.end(), name) != group.members.end();
}

bool validGroup(const GroupRow& group, const std::string& account) {
    if (group.id <= 0 || !group.revision || group.revision > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        group.name.empty() || group.name.size() > 128 || group.members.size() > 64)
        return false;
    std::set<std::string> members;
    for (std::size_t i = 0; i < group.members.size(); ++i) {
        const std::string& name = group.members[i];
        if (name.empty() || name.size() > 32 || !members.insert(name).second) return false;
    }
    return members.empty() || members.count(account) != 0;
}

bool createGroupRoster(const std::string& name, const std::vector<std::string>& requested,
                       const std::string& account, std::vector<std::string>& members) {
    GroupRow group;
    group.id = 1;
    group.revision = 1;
    group.name = name;
    group.members = requested;
    if (!groupContains(group, account)) group.members.push_back(account);
    if (!validGroup(group, account)) return false;
    members.swap(group.members);
    return true;
}

bool restoreGroupCatalog(const std::vector<GroupRow>& stored, const std::string& account,
                         GroupCatalog& output) {
    GroupCatalog restored;
    for (std::size_t i = 0; i < stored.size(); ++i) {
        GroupRow group = stored[i];
        if (!validGroup(group, account) || restored.count(group.id)) return false;
        std::sort(group.members.begin(), group.members.end());
        restored[group.id] = group;
    }
    output.swap(restored);
    return true;
}

GroupUpdate updateGroupCatalog(GroupCatalog& catalog, GroupRow group, const std::string& account) {
    if (!validGroup(group, account)) return GroupInvalid;
    std::sort(group.members.begin(), group.members.end());
    const GroupCatalog::const_iterator previous = catalog.find(group.id);
    if (previous != catalog.end() && group.revision <= previous->second.revision) {
        if (group.revision == previous->second.revision &&
            (group.name != previous->second.name || group.members != previous->second.members))
            return GroupConflict;
        return GroupOutdated;
    }
    catalog[group.id] = group;
    return GroupChanged;
}

bool reconcileGroupCatalog(GroupCatalog& catalog, const std::vector<GroupRow>& active,
                           const std::string& account) {
    std::set<int64_t> activeIds;
    for (std::size_t i = 0; i < active.size(); ++i) {
        if (!validGroup(active[i], account)) return false;
        activeIds.insert(active[i].id);
    }
    for (GroupCatalog::iterator it = catalog.begin(); it != catalog.end(); ++it) {
        if (!activeIds.count(it->first)) it->second.members.clear();
    }
    for (std::size_t i = 0; i < active.size(); ++i) catalog[active[i].id] = active[i];
    return true;
}

GroupSend planGroupSend(const GroupCatalog& groups, int64_t id, const std::string& text,
                        const std::string& account, std::vector<std::string>& recipients) {
    const GroupCatalog::const_iterator group = groups.find(id);
    if (group != groups.end() && !groupContains(group->second, account)) return GroupForbidden;
    if (group == groups.end() || text.empty() || text.size() > 65536) return GroupUnavailable;
    if (group->second.members.size() < 2) return GroupNoRecipients;

    std::vector<std::string> result;
    for (std::size_t i = 0; i < group->second.members.size(); ++i) {
        const std::string& member = group->second.members[i];
        if (member != account) result.push_back(member);
    }
    recipients.swap(result);
    return GroupReady;
}

bool retryGroup(GroupRetry& plan) {
    for (std::size_t i = 0; i < plan.size(); ++i) {
        if (!plan.sent(i) && !plan.tryAndPersist(i)) return false;
    }
    return true;
}

} // namespace chat
} // namespace nextalk
