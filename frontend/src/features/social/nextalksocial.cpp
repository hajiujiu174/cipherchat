#include "services/nextalkservice.h"
#include "services/nextalkmapping.h"
#include <QSet>
#include <algorithm>
#include <QDateTime>
#include <QTimer>
#include <QUuid>

namespace Messager {

// 搜索用户。
void NextalkService::searchUsers(const QString& query, const QString& after) {
    if (requireOnline())
        m_gateway.post({{"command", "searchUsers"}, {"query", query}, {"after", after}});
}

// 添加好友。
void NextalkService::addFriend(const QString& user) {
    if (!requireOnline())
        return;
    if (user.isEmpty() || user == m_user || user.size() > 32) {
        error("请选择有效的其他用户");
        return;
    }
    if (isFriend(user) || m_outgoing.contains(user) || friendRequestPending(user))
        return;
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingFriendRequests.insert(user, token);
    if (!m_gateway.post({{"command", "addFriend"}, {"username", user}}))
        m_pendingFriendRequests.remove(user);
    emit contactsChanged();
    QTimer::singleShot(15000, this, [this, user, token] {
        if (m_pendingFriendRequests.value(user) != token)
            return;
        m_pendingFriendRequests.remove(user);
        emit contactsChanged();
        error("好友申请暂未得到确认，请稍后重试");
    });
}

// 回应好友。
void NextalkService::respondFriend(const QString& user, bool accept) {
    if (requireOnline())
        m_gateway.post({{"command", "respondFriend"}, {"username", user}, {"accept", accept}});
}

// 更新用户资料摘要。
void NextalkService::updateProfileSummary(const QString& user,const QJsonObject& value) {
    const QJsonObject summary{{"nickname",value["nickname"]},{"avatar",value["avatar"]}};
    if(m_profileSummaries.value(user)==summary)return;
    m_profileSummaries[user]=summary;
    emit profileSummaryChanged(user);
}

// 刷新资料Summaries。
void NextalkService::refreshProfileSummaries() {
    if(!online())return;
    QStringList users{m_user};
    for(const auto& c:m_conversations) {
        if(c.group)users.append(c.members);
        else if(!c.peer.isEmpty())users.append(c.peer);
    }
    users.removeDuplicates();
    std::stable_sort(users.begin(),users.end(),[this](const QString& a,const QString& b){return m_summaryRequestedAt.value(a)<m_summaryRequestedAt.value(b);});
    const auto now=QDateTime::currentMSecsSinceEpoch();
    int sent=0;
    for(const auto& user:users) {
        if(user.isEmpty()||now-m_summaryRequestedAt.value(user)<15000)continue;
        m_summaryRequestedAt[user]=now;
        const auto view="avatar:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_summaryViews[user]=view;
        m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","get"},{"username",user},{"before",0},{"view_id",view}}}});
        if(++sent==4)break;
    }
}

// 请求资料。
void NextalkService::requestProfile(const QString& user,qint64 before) {
    if(!requireOnline())return;
    if(!before)++m_profileGenerations[user];
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","get"},{"username",user},{"before",before},{"view_id",QString::number(m_profileGenerations[user])}}}});
}

// 请求动态信息流。
void NextalkService::requestMomentsFeed(qint64 beforeMs,qint64 beforeId) {
    if(!online()){emit momentsFeedFinished(false,"请登录后查看朋友圈");return;}
    if(!beforeId)++m_feedGeneration;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","feed"},{"before_ms",beforeMs},{"before_id",beforeId},{"view_id",QString::number(m_feedGeneration)}}}});
}

// 将昵称和头像编辑结果作为资料更新请求提交给服务端。
void NextalkService::saveProfile(const QString& nickname,const QString& avatar) {
    if(!requireOnline())return;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","save"},{"nickname",nickname},{"avatar",avatar}}}});
}

// 发布动态。
void NextalkService::publishMoment(const QString& text,const QString& key) {
    if(!requireOnline())return;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","publish"},{"text",text},{"request_id",key}}}});
}

// 更新桌面联机服务中的social操作状态。
void NextalkService::socialAction(const QJsonObject& value) {
    if(requireOnline())m_gateway.post({{"command","social"},{"value",value}});
}

} // namespace Messager
