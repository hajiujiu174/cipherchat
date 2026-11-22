#include "ccfw/group_service.hpp"
#include "wire_helpers.hpp"
#include <algorithm>
#include <set>
namespace ccfw {
namespace {
// 按组整理错误。
Error groupError(ErrorCode code, const char* message) { return {code, "M5-01/02", message}; }
namespace classic = nextalk::chat;

classic::GroupRow describe(const GroupSnapshot& snapshot) {
    classic::GroupRow row;
    row.id = snapshot.id;
    row.revision = snapshot.revision;
    row.name = snapshot.name;
    row.members = snapshot.members;
    return row;
}

GroupSnapshot snapshot(const classic::GroupRow& row) {
    return {row.id, row.revision, row.name, row.members};
}

std::vector<classic::GroupRow> describe(const std::vector<GroupSnapshot>& groups) {
    std::vector<classic::GroupRow> rows;
    rows.reserve(groups.size());
    for (const auto& group : groups) rows.push_back(describe(group));
    return rows;
}

// 将现有聊天发送和本地提交接到逐成员重试循环。
class RetryAdapter final : public classic::GroupRetry {
public:
    explicit RetryAdapter(const GroupDraft& draft, std::function<bool(std::size_t)> attempt)
        : draft_(draft), attempt_(std::move(attempt)) {}
    std::size_t size() const override { return draft_.report.members.size(); }
    bool sent(std::size_t index) const override { return draft_.report.members[index].messageId.has_value(); }
    bool tryAndPersist(std::size_t index) override { return attempt_(index); }
private:
    // 注入初始副本，后续提交会替换 GroupService 的 drafts_ 容器。
    GroupDraft draft_;
    std::function<bool(std::size_t)> attempt_;
};
}
// 构造群聊服务，初始化后续操作需要的状态或依赖。
GroupService::GroupService(IChatService& chat, ITransport& net, AccountScope account, ILocalStore* store, Clock clock)
    : chat_(chat), net_(net), account_(std::move(account)), store_(store), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}
// 恢复群聊服务。
Result<> GroupService::restore() {
    if (restored_) return Result<>::success({});
    if (store_) {
        auto loaded = store_->load(); if (!loaded) return Result<>::failure(loaded.error());
        const auto& snapshot = loaded.value();
        if (snapshot.account.username != account_.username || snapshot.account.server != account_.server)
            return Result<>::failure(groupError(ErrorCode::StorageFailure, "群快照账号不匹配"));
        if (!classic::restoreGroupCatalog(describe(snapshot.groups), account_.username, groups_))
            return Result<>::failure(groupError(ErrorCode::StorageFailure, "群快照损坏"));
        drafts_ = snapshot.groupDrafts;
    }
    restored_ = true; return Result<>::success({});
}
// 持久化群聊服务。
Result<> GroupService::persist(const classic::GroupCatalog& groups, const std::vector<GroupDraft>& drafts) {
    if (store_) {
        auto loaded = store_->load(); if (!loaded) return Result<>::failure(loaded.error()); auto next = loaded.value();
        next.groups.clear(); for (const auto& [_, group] : groups) next.groups.push_back(snapshot(group));
        next.groupDrafts = drafts; const auto revision = next.revision++; auto saved = store_->commit(revision, next); if (!saved) return saved;
    }
    groups_ = groups; drafts_ = drafts; return Result<>::success({});
}
// 创建群聊服务。
Result<> GroupService::create(const std::string& name, const std::vector<std::string>& members) {
    std::vector<std::string> roster;
    if (!classic::createGroupRoster(name, members, account_.username, roster))
        return Result<>::failure(groupError(ErrorCode::InvalidArgument, "群名或成员无效，群成员须 2–64 人且不重复"));
    try { (void)nlohmann::json(name).dump(); }
    catch (const std::exception&) { return Result<>::failure(groupError(ErrorCode::InvalidArgument, "群名不是有效文本")); }
    return net_.send(cc::proto::withV2(cc::proto::makeCreateGroup(name, roster)));
}
// 核对群 ID、成员和修订号后更新本机的群快照。
Result<> GroupService::applySnapshot(const GroupSnapshot& snapshot) {
    auto restored = restore(); if (!restored) return restored;
    auto next = groups_;
    switch (classic::updateGroupCatalog(next, describe(snapshot), account_.username)) {
    case classic::GroupInvalid:
        return Result<>::failure(groupError(ErrorCode::ProtocolError, "群快照字段或成员非法"));
    case classic::GroupConflict:
        return Result<>::failure(groupError(ErrorCode::Conflict, "同版本群快照内容冲突"));
    case classic::GroupOutdated:
        return Result<>::failure(groupError(ErrorCode::AlreadyProcessed, "已保存更新或相同的群快照"));
    case classic::GroupChanged:
        return persist(next, drafts_);
    }
    return Result<>::failure(groupError(ErrorCode::ProtocolError, "群快照状态无效"));
}
// 同步校准群聊服务。
Result<> GroupService::reconcile(const std::vector<GroupSnapshot>& active) {
    auto restored = restore();
    if (!restored) return restored;
    auto next = groups_;
    if (!classic::reconcileGroupCatalog(next, describe(active), account_.username))
        return Result<>::failure(groupError(ErrorCode::ProtocolError, "群目录无效"));
    return persist(next, drafts_);
}
// 收集当前已加载的群快照，供界面恢复群目录。
Result<std::vector<GroupSnapshot>> GroupService::snapshots() {
    auto restored = restore(); if (!restored) return Result<std::vector<GroupSnapshot>>::failure(restored.error());
    std::vector<GroupSnapshot> result; for (const auto& [_, group] : groups_) result.push_back(snapshot(group));
    return Result<std::vector<GroupSnapshot>>::success(std::move(result));
}
// 按逻辑群消息 ID 保存逐成员投递计划，再为其他成员分别发出消息。
Result<GroupSendReport> GroupService::sendText(std::int64_t id, const std::string& text) {
    auto restored = restore(); if (!restored) return Result<GroupSendReport>::failure(restored.error());
    std::vector<std::string> recipients;
    switch (classic::planGroupSend(groups_, id, text, account_.username, recipients)) {
    case classic::GroupUnavailable:
        return Result<GroupSendReport>::failure(groupError(ErrorCode::InvalidArgument, "群不存在或文本无效"));
    case classic::GroupForbidden:
        return Result<GroupSendReport>::failure(groupError(ErrorCode::Forbidden, "已离开群聊"));
    case classic::GroupNoRecipients:
        return Result<GroupSendReport>::failure(groupError(ErrorCode::Forbidden, "已离开、群已解散或没有其他成员"));
    case classic::GroupReady:
        break;
    }
    GroupDraft draft{id, text, {wire::id(), {}}};
    for (const auto& recipient : recipients) draft.report.members.push_back({recipient, std::nullopt, "尚未排队"});
    auto next = drafts_; next.push_back(draft); auto saved = persist(groups_, next);
    if (!saved) return Result<GroupSendReport>::failure(saved.error());
    return retry(draft.report.logicalMessageId);
}
// 从持久化群发送计划找出仍缺少投递 ID 的成员，逐项重试并保存进度。
Result<GroupSendReport> GroupService::retry(const std::string& logicalId) {
    auto restored = restore(); if (!restored) return Result<GroupSendReport>::failure(restored.error());
    auto found = std::find_if(drafts_.begin(), drafts_.end(), [&](const auto& d) { return d.report.logicalMessageId == logicalId; });
    if (found == drafts_.end()) return Result<GroupSendReport>::failure(groupError(ErrorCode::NotFound, "群发送记录不存在"));
    const auto index = static_cast<std::size_t>(found - drafts_.begin());
    auto saved = Result<>::success({});
    RetryAdapter pending(drafts_[index], [&](std::size_t member) {
        auto next = drafts_; auto& draft = next[index]; auto& result = draft.report.members[member];
        const auto group = groups_.find(draft.groupId);
        if (group == groups_.end() || !classic::groupContains(group->second, result.recipient)) result.error = "该成员已不在群中";
        else {
            auto sent = chat_.sendText({result.recipient, draft.text, draft.groupId, logicalId});
            if (sent) { result.messageId = sent.value(); result.error.clear(); } else result.error = sent.error().message;
        }
        saved = persist(groups_, next);
        return saved.ok();
    });
    if (!classic::retryGroup(pending)) return Result<GroupSendReport>::failure(saved.error());
    return Result<GroupSendReport>::success(drafts_[index].report);
}
// 发送正在输入。
Result<> GroupService::sendTyping(const std::string& peer) {
    if (peer.empty() || peer.size() > 32 || peer == account_.username) return Result<>::failure(groupError(ErrorCode::InvalidArgument, "输入提示对端无效"));
    const auto now = clock_(); const auto previous = lastTyping_.find(peer);
    if (previous != lastTyping_.end() && now - previous->second < 1000) return Result<>::success({});
    auto sent = net_.send(cc::proto::withV2(cc::proto::makeTypingTo(peer)));
    if (sent) { std::erase_if(lastTyping_, [&](const auto& p) { return now - p.second > 3000; }); lastTyping_[peer] = now; }
    return sent;
}
}
