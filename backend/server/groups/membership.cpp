#include "server/router/router.hpp"
#include "server/router/validation.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace cc::server {

using cc::proto::bodyStr;

// 验证创建者、群名及成员列表，持久化并向在线成员发送群信息。
void Router::handleCreateGroup(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "创建群组")) return;
    const std::string name = bodyStr(msg.body, "name");
    if (usesV2(peer)) {
        const auto list = msg.body.find("members");
        if (name.size() > 128 || list == msg.body.end() || !list->is_array() || list->size() > 64 ||
            std::any_of(list->begin(), list->end(), [](const auto& member) { return !member.is_string() || member.template get<std::string>().empty() || member.template get<std::string>().size() > 32; })) {
            deliver(peer, cc::proto::makeError("protocol_error", "群名或成员字段非法")); return;
        }
    }
    std::vector<std::string> members;
    if (auto it = msg.body.find("members"); it != msg.body.end() &&
                                             it->is_array()) {
        for (const auto& m : *it) {
            if (m.is_string()) members.push_back(m.get<std::string>());
        }
    }
    // 创建者自动加入；加入后成员须 2-64 人
    auto unique = members; std::sort(unique.begin(), unique.end());
    if (name.empty() || members.size() > 64 || std::adjacent_find(unique.begin(), unique.end()) != unique.end()) {
        deliver(peer, cc::proto::makeGroupResult(
            false, "群名不能为空且成员须 2-64 人", 0, "", {}));
        return;
    }
    if (std::find(members.begin(), members.end(), peer.username()) ==
        members.end()) {
        members.push_back(peer.username());
    }
    if (members.size() < 2 || members.size() > 64) {
        deliver(peer, cc::proto::makeGroupResult(
            false, "群名不能为空且成员须 2-64 人", 0, "", {}));
        return;
    }
    for (const auto& m : members) {
        if (!db_.findAccount(m)) {
            deliver(peer, cc::proto::makeGroupResult(
                false, "成员不存在: " + m, 0, "", {}));
            return;
        }
    }
    std::erase(members,peer.username());members.insert(members.begin(),peer.username());
    auto gid = db_.createGroup(name, members);
    if (!gid) {
        deliver(peer, cc::proto::makeGroupResult(false, "创建失败", 0, "", {}));
        return;
    }
    deliver(peer, cc::proto::makeGroupResult(true, "", *gid, name, members));
    // 通知其他在线成员
    for (const auto& m : members) {
        if (m != peer.username()) {
            if (Peer* p = findOnline(m)) {
                deliver(*p, cc::proto::makeGroupUpdate(*gid, name, members));
            }
        }
    }
}

} // namespace cc::server
