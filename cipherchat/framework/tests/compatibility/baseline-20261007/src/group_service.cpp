#include "ccfw/group_service.hpp"
#include "wire_helpers.hpp"
#include <algorithm>
#include <set>
namespace ccfw {
namespace {
Error groupError(ErrorCode code, const char* message) { return {code, "M5-01/02", message}; }
bool valid(const GroupSnapshot& group, const AccountScope& account) {
    std::set<std::string> members;
    for (const auto& member : group.members) if (member.empty() || member.size() > 32 || !members.insert(member).second) return false;
    return group.id > 0 && group.revision > 0 && group.revision <= INT64_MAX && !group.name.empty() && group.name.size() <= 128 &&
        members.size() >= 2 && members.size() <= 64 && members.count(account.username);
}
}
GroupService::GroupService(IChatService& chat, ITransport& net, AccountScope account, ILocalStore* store, Clock clock)
    : chat_(chat), net_(net), account_(std::move(account)), store_(store), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}
Result<> GroupService::restore() {
    if (restored_) return Result<>::success({});
    if (store_) {
        auto loaded = store_->load(); if (!loaded) return Result<>::failure(loaded.error());
        const auto& snapshot = loaded.value();
        if (snapshot.account.username != account_.username || snapshot.account.server != account_.server)
            return Result<>::failure(groupError(ErrorCode::StorageFailure, "群快照账号不匹配"));
        std::map<std::int64_t, GroupSnapshot> groups;
        for (auto group : snapshot.groups) {
            if (!valid(group, account_) || groups.count(group.id)) return Result<>::failure(groupError(ErrorCode::StorageFailure, "群快照损坏"));
            std::sort(group.members.begin(), group.members.end()); groups[group.id] = std::move(group);
        }
        groups_ = std::move(groups); drafts_ = snapshot.groupDrafts;
    }
    restored_ = true; return Result<>::success({});
}
Result<> GroupService::persist(const std::map<std::int64_t, GroupSnapshot>& groups, const std::vector<GroupDraft>& drafts) {
    if (store_) {
        auto loaded = store_->load(); if (!loaded) return Result<>::failure(loaded.error()); auto next = loaded.value();
        next.groups.clear(); for (const auto& [_, group] : groups) next.groups.push_back(group);
        next.groupDrafts = drafts; const auto revision = next.revision++; auto saved = store_->commit(revision, next); if (!saved) return saved;
    }
    groups_ = groups; drafts_ = drafts; return Result<>::success({});
}
Result<> GroupService::create(const std::string& name, const std::vector<std::string>& members) {
    GroupSnapshot group{1, 1, name, members};
    if (std::find(group.members.begin(), group.members.end(), account_.username) == group.members.end()) group.members.push_back(account_.username);
    if (!valid(group, account_)) return Result<>::failure(groupError(ErrorCode::InvalidArgument, "群名或成员无效，群成员须 2–64 人且不重复"));
    try { (void)nlohmann::json(name).dump(); }
    catch (const std::exception&) { return Result<>::failure(groupError(ErrorCode::InvalidArgument, "群名不是有效文本")); }
    return net_.send(cc::proto::withV2(cc::proto::makeCreateGroup(name, group.members)));
}
Result<> GroupService::applySnapshot(const GroupSnapshot& snapshot) {
    auto restored = restore(); if (!restored) return restored;
    if (!valid(snapshot, account_)) return Result<>::failure(groupError(ErrorCode::ProtocolError, "群快照字段或成员非法"));
    auto group = snapshot; std::sort(group.members.begin(), group.members.end());
    const auto previous = groups_.find(group.id);
    if (previous != groups_.end() && group.revision <= previous->second.revision) {
        if (group.revision == previous->second.revision && (group.name != previous->second.name || group.members != previous->second.members))
            return Result<>::failure(groupError(ErrorCode::Conflict, "同版本群快照内容冲突"));
        return Result<>::failure(groupError(ErrorCode::AlreadyProcessed, "已保存更新或相同的群快照"));
    }
    auto next = groups_; next[group.id] = std::move(group); return persist(next, drafts_);
}
Result<std::vector<GroupSnapshot>> GroupService::snapshots() {
    auto restored = restore(); if (!restored) return Result<std::vector<GroupSnapshot>>::failure(restored.error());
    std::vector<GroupSnapshot> result; for (const auto& [_, group] : groups_) result.push_back(group);
    return Result<std::vector<GroupSnapshot>>::success(std::move(result));
}
Result<GroupSendReport> GroupService::sendText(std::int64_t id, const std::string& text) {
    auto restored = restore(); if (!restored) return Result<GroupSendReport>::failure(restored.error());
    const auto group = groups_.find(id);
    if (group == groups_.end() || text.empty() || text.size() > 65536) return Result<GroupSendReport>::failure(groupError(ErrorCode::InvalidArgument, "群不存在或文本无效"));
    GroupDraft draft{id, text, {wire::id(), {}}};
    for (const auto& member : group->second.members) if (member != account_.username) draft.report.members.push_back({member, std::nullopt, "尚未排队"});
    auto next = drafts_; next.push_back(draft); auto saved = persist(groups_, next);
    if (!saved) return Result<GroupSendReport>::failure(saved.error());
    return retry(draft.report.logicalMessageId);
}
Result<GroupSendReport> GroupService::retry(const std::string& logicalId) {
    auto restored = restore(); if (!restored) return Result<GroupSendReport>::failure(restored.error());
    auto found = std::find_if(drafts_.begin(), drafts_.end(), [&](const auto& d) { return d.report.logicalMessageId == logicalId; });
    if (found == drafts_.end()) return Result<GroupSendReport>::failure(groupError(ErrorCode::NotFound, "群发送记录不存在"));
    const auto index = static_cast<std::size_t>(found - drafts_.begin());
    for (std::size_t member = 0; member < drafts_[index].report.members.size(); ++member) {
        if (drafts_[index].report.members[member].messageId) continue;
        auto next = drafts_; auto& draft = next[index]; auto& result = draft.report.members[member];
        const auto group = groups_.find(draft.groupId);
        if (group == groups_.end() || std::find(group->second.members.begin(), group->second.members.end(), result.recipient) == group->second.members.end()) result.error = "该成员已不在群中";
        else {
            auto sent = chat_.sendText({result.recipient, draft.text, draft.groupId, logicalId});
            if (sent) { result.messageId = sent.value(); result.error.clear(); } else result.error = sent.error().message;
        }
        auto saved = persist(groups_, next); if (!saved) return Result<GroupSendReport>::failure(saved.error());
    }
    return Result<GroupSendReport>::success(drafts_[index].report);
}
Result<> GroupService::sendTyping(const std::string& peer) {
    if (peer.empty() || peer.size() > 32 || peer == account_.username) return Result<>::failure(groupError(ErrorCode::InvalidArgument, "输入提示对端无效"));
    const auto now = clock_(); const auto previous = lastTyping_.find(peer);
    if (previous != lastTyping_.end() && now - previous->second < 1000) return Result<>::success({});
    auto sent = net_.send(cc::proto::withV2(cc::proto::makeTypingTo(peer)));
    if (sent) { std::erase_if(lastTyping_, [&](const auto& p) { return now - p.second > 3000; }); lastTyping_[peer] = now; }
    return sent;
}
}
