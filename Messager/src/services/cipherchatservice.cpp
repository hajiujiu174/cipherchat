#include "cipherchatservice.h"
#include "gomokuwire.h"
#include "cipherchatmapping.h"
#include "localfiles.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QTimer>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>
namespace Messager {
namespace {
QString nowText() {
    return QTime::currentTime().toString("HH:mm");
}
QStringList strings(const QJsonValue& v) {
    QStringList r;
    for (const auto& x : v.toArray())
        r << x.toString();
    return r;
}
QVector<Contact> contactList(const QJsonArray& a, bool friends) {
    QVector<Contact> r;
    for (auto v : a) {
        auto p = v.toArray();
        if (p.size() == 2)
            r.push_back({p[0].toString(), p[1].toBool(), friends});
    }
    return r;
}
} // namespace
CipherChatService::CipherChatService(QJsonObject config, QObject* parent)
    : ChatService(parent), m_config(config) {
    m_config.remove("password");
    connect(&m_gateway, &CipherChatGateway::eventReceived, this, &CipherChatService::handle);
    connect(&m_gateway, &CipherChatGateway::commandRejected, this, &CipherChatService::error);
    auto* profileTimer = new QTimer(this);
    profileTimer->setInterval(1000);
    connect(profileTimer, &QTimer::timeout, this, &CipherChatService::refreshProfileSummaries);
    profileTimer->start();
}
CipherChatService::~CipherChatService() = default;
void CipherChatService::resetView() {
    m_conversations.clear();
    m_messages.clear();
    m_transfers.clear();
    m_contacts.clear();
    m_searchResults.clear();
    m_profiles.clear();m_profileGenerations.clear();
    m_profileSummaries.clear();m_summaryViews.clear();m_summaryRequestedAt.clear();
    m_momentsFeed={};++m_feedGeneration;emit momentsFeedChanged();
    m_records.clear();
    m_memberLogical.clear();
    m_groupDelivery.clear();
    m_groupPending.clear();
    m_activities.clear();
    m_incoming.clear();
    m_outgoing.clear();
    m_pendingFriendRequests.clear();
    m_viewing.clear();
    m_searchNext.clear();
    emit conversationsChanged();
    emit contactsChanged();
    emit transfersChanged();
    emit activitiesChanged();
}
void CipherChatService::requestRegistrationCode(const QJsonObject& request) {
    auto command = request;
    command["requestCodeOnly"] = true; command["registerNew"] = false; command["password"] = "";
    authenticate(command);
}
void CipherChatService::resetPassword(const QJsonObject& request) {
    auto command=request; command["passwordReset"]=true; command["registerNew"]=false; command["requestCodeOnly"]=false;
    authenticate(command);
}
void CipherChatService::authenticate(const QJsonObject& request) {
    if (request["username"].toString().trimmed().isEmpty() || (!request["requestCodeOnly"].toBool() && !request["emailLogin"].toBool() && request["password"].toString().isEmpty())) {
        error("请输入账号和密码");
        return;
    }
    if (!QFileInfo::exists(request["caFile"].toString())) {
        error("未找到服务器证书，请检查连接设置");
        return;
    }
    QJsonObject command = request;
    command["command"] = "authenticate";
    m_config = request;
    m_config.remove("password");
    m_config.remove("emailCode");
    m_config.remove("email");
    m_config.remove("requestCodeOnly");
    m_config.remove("passwordReset");
    m_config.remove("emailLogin");
    m_config.remove("registerNew");
    m_config.remove("command");
    m_user = request["username"].toString().trimmed();
    m_online = false;
    resetView();
    m_status = "正在验证服务器连接…";
    emit sessionChanged();
    if (!m_gateway.post(command)) {
        m_status = "连接请求未提交";
        emit sessionChanged();
    }
}
void CipherChatService::disconnectAccount() {
    m_gateway.post({{"command", "disconnect"}});
    m_online = false;
    resetView();
    m_status = "已退出登录";
    emit sessionChanged();
}
bool CipherChatService::requireOnline() {
    if (m_online)
        return true;
    error("请先登录 CipherChat 后再操作");
    return false;
}
void CipherChatService::ensurePeer(const QString& peer) {
    if (peer.isEmpty() || peer == m_user)
        return;
    const QString id = "peer:" + peer;
    for (const auto& c : m_conversations)
        if (c.id == id)
            return;
    Conversation c;
    c.id = id;
    c.peer = peer;
    c.name = peer;
    c.initials = peer.left(1).toUpper();
    c.color = QColor::fromHsv(int(qHash(peer) % 360), 95, 170);
    c.subtitle = "临时会话 · 仅限文字";
    m_conversations.push_back(c);
}
void CipherChatService::refreshPeerConversations() {
    for (auto& conversation : m_conversations) {
        if (conversation.group || conversation.peer.isEmpty())
            continue;
        conversation.subtitle = "临时会话 · 仅限文字";
        for (const auto& contact : m_contacts)
            if (contact.username == conversation.peer && contact.friendContact) {
                conversation.subtitle = contact.online ? "在线 · 好友" : "离线 · 好友";
                break;
            }
    }
}
QString CipherChatService::openDirectConversation(const QString& username) {
    if (!requireOnline())
        return {};
    const QString peer = username.trimmed();
    if (peer.isEmpty() || peer == m_user) {
        error("请选择其他用户开启会话");
        return {};
    }
    const QString id = "peer:" + peer;
    const bool created = conversation(id).id.isEmpty();
    ensurePeer(peer);
    refreshPeerConversations();
    if (created)
        emit conversationsChanged();
    return id;
}
bool CipherChatService::canSendFiles(const QString& id) const {
    const auto c = conversation(id);
    return ChatService::canSendFiles(id) && !c.peer.isEmpty() && isFriend(c.peer);
}
Conversation CipherChatService::conversation(const QString& id) const {
    for (const auto& c : m_conversations)
        if (c.id == id)
            return c;
    return {};
}
QVector<Message> CipherChatService::messages(const QString& id) const {
    QVector<Message> r;
    for (const auto& m : m_messages)
        if (m.conversation == id)
            r << m;
    std::stable_sort(r.begin(), r.end(),
                     [](const Message& a, const Message& b) { return a.timestamp < b.timestamp; });
    return r;
}
QString CipherChatService::send(const QString& id, const QString& text, bool) {
    if (!requireOnline() || text.trimmed().isEmpty())
        return {};
    const auto c = conversation(id);
    if (c.id.isEmpty()) {
        error("请选择联系人或群聊");
        return {};
    }
    const QString rid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto command =
        c.group ? CipherChatMapping::sendGroup(c.groupId, text) : CipherChatMapping::sendText(c.peer, text);
    if (!c.group)
        command["messageId"] = rid;
    if (!m_gateway.post(command, rid))
        return {};
    Message m;
    m.id = rid;
    m.conversation = id;
    m.sender = m_user;
    m.text = text;
    m.time = nowText();
    m.outgoing = true;
    m.state = SendState::Sending;
    m.transportStatus = "已提交业务队列";
    m.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_messages << m;
    if (c.group)
        m_groupPending << rid;
    else
        m_records[rid] = {{"from", m_user},   {"to", c.peer}, {"text", text},
                          {"messageId", rid}, {"groupId", 0}, {"timestamp", m.timestamp},
                          {"state", 0}};
    emit messagesChanged(id);
    return rid;
}
void CipherChatService::retryMessage(const QString& id) {
    if (!requireOnline())
        return;
    if (m_records.contains(id)) {
        auto record = m_records.value(id);
        auto command = CipherChatMapping::sendText(record["to"].toString(), record["text"].toString());
        command["messageId"] = id;
        m_gateway.post(command, id);
    } else
        m_gateway.post({{"command", "retryGroup"}, {"logicalId", id}});
}
void CipherChatService::markViewed(const QString& id) {
    m_viewing = id;
    for (auto& c : m_conversations)
        if (c.id == id && c.unread) {
            c.unread = 0;
            emit conversationsChanged();
            break;
        }
}
void CipherChatService::simulateIncoming(const QString&) {
    error("真实模式不生成模拟消息");
}
void CipherChatService::addTransfer(const QString&, qint64, const QString&) {
    error("请通过文件选择器发送真实文件");
}
void CipherChatService::sendFile(const QString& path, const QString& id) {
    if (!requireOnline())
        return;
    auto c = conversation(id);
    if (c.group || c.peer.isEmpty()) {
        error("当前后端的文件发送需要选择一位联系人");
        return;
    }
    if (!canSendFiles(id)) {
        error("临时会话仅支持文字消息，成为好友后才能发送文件");
        return;
    }
    if (!QFileInfo(path).isFile()) {
        error("文件不存在或不是普通文件");
        return;
    }
    m_gateway.post(CipherChatMapping::sendFile(c.peer, path),
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
}
void CipherChatService::transferAction(const QString& id, const QString& action) {
    for (const auto& t : m_transfers)
        if (t.id == id) {
            if (action == "reveal") {
                if (!revealLocalFile(t.path))
                    error("本地文件位置不可用，无法打开所在文件夹");
                return;
            }
            if (action == "open") {
                if (t.state == TransferState::Completed && QFileInfo::exists(t.path))
                    QDesktopServices::openUrl(QUrl::fromLocalFile(t.path));
                else
                    error("文件尚未完成或本地路径不存在");
                return;
            }
            if (!requireOnline())
                return;
            if (action == "resume" && t.outgoing && !canSendFiles(t.conversation)) {
                error("临时会话仅支持文字消息，成为好友后才能发送文件");
                return;
            }
            if (action == "resume" && t.state != TransferState::Cancelled)
                m_gateway.post({{"command", "resumeFile"}, {"fileId", id}});
            else if (action == "cancel")
                m_gateway.post({{"command", "cancelFile"}, {"fileId", id}});
            else
                error("此状态不支持该操作；已取消文件需要重新选择发送");
            return;
        }
}
void CipherChatService::searchUsers(const QString& query, const QString& after) {
    if (requireOnline())
        m_gateway.post({{"command", "searchUsers"}, {"query", query}, {"after", after}});
}
void CipherChatService::addFriend(const QString& user) {
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
void CipherChatService::respondFriend(const QString& user, bool accept) {
    if (requireOnline())
        m_gateway.post({{"command", "respondFriend"}, {"username", user}, {"accept", accept}});
}
void CipherChatService::createGroup(const QString& name, const QStringList& members) {
    if (requireOnline())
        m_gateway.post(
            {{"command", "createGroup"}, {"name", name}, {"members", QJsonArray::fromStringList(members)}});
}
void CipherChatService::markActivitiesViewed() {
    for (auto& a : m_activities)
        a.viewed = true;
    emit activitiesChanged();
}
void CipherChatService::activity(const QString& title, const QString& text, const QString& id) {
    m_activities.prepend({title, text, id, false});
    if (m_activities.size() > 150)
        m_activities.removeLast();
    emit activitiesChanged();
}
void CipherChatService::error(const QString& message) {
    activity("操作提示", message);
    emit operationError(message);
}
void CipherChatService::applyGroup(const QJsonObject& o) {
    const qint64 groupId = o["id"].toInteger();
    const QString id = "group:" + QString::number(groupId);
    Conversation c = conversation(id);
    c.id = id;
    c.group = true;
    c.groupId = groupId;
    c.name = o["name"].toString();
    c.initials = c.name.left(1);
    c.color = QColor("#6554d9");
    c.members = strings(o["members"]);
    c.subtitle = QString::number(c.members.size()) + " 位成员";
    for (auto& old : m_conversations)
        if (old.id == id) {
            old = c;
            return;
        }
    m_conversations << c;
}
void CipherChatService::applyRecord(const QJsonObject& o, bool restored) {
    Message m = CipherChatMapping::message(o, m_user);
    m.timestamp = o["timestamp"].toInteger();
    m_records[m.id] = o;
    if (o["groupId"].toInteger()) {
        if (conversation(m.conversation).id.isEmpty())
            applyGroup({{"id", o["groupId"]},
                        {"name", "群聊 " + QString::number(o["groupId"].toInteger())},
                        {"members", QJsonArray{}}});
        QString logical = o["logicalGroupMessageId"].toString();
        if (!logical.isEmpty()) {
            m_memberLogical[m.id] = logical;
            if (m.outgoing) {
                m_groupDelivery[logical][o["to"].toString()] = o["state"].toInt();
                auto states = m_groupDelivery.value(logical);
                int delivered = 0, failed = 0;
                for (auto state : states) {
                    delivered += state == 2;
                    failed += state == 3;
                }
                m.transportStatus =
                    QString("对端签收 %1 / %2 人")
                        .arg(delivered)
                        .arg(qMax(states.size(), qMax(0, conversation(m.conversation).members.size() - 1)));
                m.state = failed                                                         ? SendState::Failed
                          : delivered == conversation(m.conversation).members.size() - 1 ? SendState::Stored
                                                                                         : SendState::Sending;
            }
            m.id = logical;
        }
    } else
        ensurePeer(m.outgoing ? o["to"].toString() : m.sender);
    bool exists = false;
    for (auto& old : m_messages)
        if (old.id == m.id) {
            old = m;
            exists = true;
            break;
        }
    if (!exists)
        m_messages << m;
    const bool gameControl=GomokuWire::isControl(m.text);
    if(!gameControl)for (auto& c : m_conversations)
        if (c.id == m.conversation) {
            c.preview = (m.outgoing ? "我：" : m.sender + "：") + m.text.simplified();
            if(GomokuWire::decode(m.text)["type"]=="invite")c.preview="五子棋邀请 · 点击加入房间";
            c.time = m.time;
            if (!exists && !restored && !m.outgoing && m_viewing != c.id)
                ++c.unread;
        }
    if (!gameControl && !exists && !restored && !m.outgoing)
        activity("收到新消息", m.sender + " 发来一条消息", m.conversation);
    if(!gameControl)emit conversationsChanged();
    emit messagesChanged(m.conversation);
}
void CipherChatService::applyFile(const QJsonObject& o, bool restored) {
    auto t = CipherChatMapping::file(o);
    t.simulated = false;
    t.outgoing = o["outgoing"].toBool();
    t.path = o["path"].toString();
    t.timestamp = o["timestamp"].toInteger();
    if (restored && t.state == TransferState::Running)
        t.state = TransferState::Paused;
    ensurePeer(o["peer"].toString());
    bool exists = false;
    for (auto& old : m_transfers)
        if (old.id == t.id) {
            old = t;
            exists = true;
            break;
        }
    if (!exists)
        m_transfers.prepend(t);
    Message m;
    m.id = t.messageId;
    m.file = true;
    m.text = t.name;
    m.conversation = t.conversation;
    m.sender = t.outgoing ? m_user : o["peer"].toString();
    m.outgoing = t.outgoing;
    m.timestamp = t.timestamp;
    m.time = t.timestamp ? QDateTime::fromMSecsSinceEpoch(t.timestamp).toString("HH:mm") : "时间未记录";
    m.transportStatus = t.state == TransferState::Completed
                            ? (t.outgoing ? "对端确认文件接收" : "文件校验完成")
                        : t.state == TransferState::Cancelled ? "已取消"
                                                              : QString("文件 · %1%").arg(t.progress);
    bool had = false;
    for (auto& old : m_messages)
        if (old.id == m.id) {
            old = m;
            had = true;
            break;
        }
    if (!had)
        m_messages << m;
    if (!exists && !restored)
        activity("文件传输", t.name, t.conversation);
    emit conversationsChanged();
    emit transfersChanged();
    emit messagesChanged(t.conversation);
}
void CipherChatService::updateProfileSummary(const QString& user,const QJsonObject& value) {
    const QJsonObject summary{{"nickname",value["nickname"]},{"avatar",value["avatar"]}};
    if(m_profileSummaries.value(user)==summary)return;
    m_profileSummaries[user]=summary;
    emit profileSummaryChanged(user);
}
void CipherChatService::refreshProfileSummaries() {
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
void CipherChatService::requestProfile(const QString& user,qint64 before) {
    if(!requireOnline())return;
    if(!before)++m_profileGenerations[user];
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","get"},{"username",user},{"before",before},{"view_id",QString::number(m_profileGenerations[user])}}}});
}
void CipherChatService::requestMomentsFeed(qint64 beforeMs,qint64 beforeId) {
    if(!online()){emit momentsFeedFinished(false,"请登录后查看朋友圈");return;}
    if(!beforeId)++m_feedGeneration;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","feed"},{"before_ms",beforeMs},{"before_id",beforeId},{"view_id",QString::number(m_feedGeneration)}}}});
}
void CipherChatService::saveProfile(const QString& nickname,const QString& avatar) {
    if(!requireOnline())return;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","save"},{"nickname",nickname},{"avatar",avatar}}}});
}
void CipherChatService::publishMoment(const QString& text,const QString& key) {
    if(!requireOnline())return;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","publish"},{"text",text},{"request_id",key}}}});
}
void CipherChatService::handle(const QJsonObject& event) {
    const QString type = event["type"].toString();
    if(type=="social") {
        auto value=event["value"].toObject();const auto op=value["op"].toString(),user=value["username"].toString();
        const bool ok=value["ok"].toBool();
        if(op=="get"&&value["view_id"].toString().startsWith("avatar:")) {
            if(!online()||m_summaryViews.value(user)!=value["view_id"].toString())return;
            m_summaryViews.remove(user);
            if(ok)updateProfileSummary(user,value);
            return;
        }
        if(op=="feed") {
            if(!online()||value["view_id"].toString()!=QString::number(m_feedGeneration))return;
            if(!ok){emit momentsFeedFinished(false,"朋友圈加载失败，请刷新重试");return;}
            if(value["before_id"].toDouble()>0) {
                auto posts=m_momentsFeed["posts"].toArray();QSet<qint64> ids;
                for(auto p:posts)ids.insert(qint64(p.toObject()["id"].toDouble()));
                for(auto p:value["posts"].toArray())if(!ids.contains(qint64(p.toObject()["id"].toDouble())))posts.append(p);
                value["posts"]=posts;
            }
            m_momentsFeed=value;emit momentsFeedChanged();emit momentsFeedFinished(true,{});return;
        }
        if(!ok){
            const QString code=value["error"].toString();QString message="资料操作未完成，请稍后重试";
            if(code=="text_too_long")message="昵称最多32个字，朋友圈最多2000个字";
            else if(code=="empty_text"||code=="invalid_text")message="内容不能为空，也不能包含控制字符";
            else if(code=="invalid_avatar")message="头像格式有误，请重新选择图片";
            else if(code=="not_found")message="此账号不存在";
            else if(code=="forbidden")message="只能修改自己的个人资料和朋友圈";
            emit profileOperationFinished(op,user,false,message);return;
        }
        if(op=="get") {
            if(value["view_id"].toString()!=QString::number(m_profileGenerations[user]))return;
            if(value["before"].toDouble()>0 && value["can_view"].toBool()) {
                auto previous=m_profiles.value(user)["posts"].toArray();QSet<qint64> ids;for(auto p:previous)ids.insert(qint64(p.toObject()["id"].toDouble()));
                for(auto p:value["posts"].toArray())if(!ids.contains(qint64(p.toObject()["id"].toDouble())))previous.append(p);
                value["posts"]=previous;
            }
            if(m_profiles.size()>=32&&!m_profiles.contains(user)){for(auto it=m_profiles.begin();it!=m_profiles.end();++it)if(it.key()!=m_user){m_profiles.erase(it);break;}}
            m_profiles[user]=value;
            updateProfileSummary(user,value);
            for(auto& c:m_conversations)if(!c.group&&c.peer==user)c.name=value["nickname"].toString(user);
            emit profileChanged(user);
        } else { emit profileOperationFinished(op,user,true,{});requestProfile(user); }
        return;
    }
    if (type == "passwordResetResult") {
        m_status = event["ok"].toBool() ? "密码已重置，请重新登录" : "密码重置未成功";
        emit passwordResetResult(event["ok"].toBool(),event["error"].toString());
        return;
    }
    if (type == "registrationCode") {
        emit registrationCodeResult(event["ok"].toBool(), event["email"].toString(), event["error"].toString(), event["retryAfter"].toInt());
        return;
    }
    if (type == "restored") {
        resetView();
        m_user = event["username"].toString();
        for (auto g : event["groups"].toArray())
            applyGroup(g.toObject());
        for (auto m : event["history"].toArray())
            applyRecord(m.toObject(), true);
        for (auto f : event["files"].toArray())
            applyFile(f.toObject(), true);
        emit conversationsChanged();
        emit sessionChanged();
        return;
    }
    if (type == "account") {
        const int phase = event["phase"].toInt();
        m_online = phase == 6;
        if (!m_online && !m_pendingFriendRequests.isEmpty()) {
            m_pendingFriendRequests.clear();
            emit contactsChanged();
        }
        switch (phase) {
        case 0:
            m_status = "连接已断开";
            break;
        case 1:
        case 2:
            m_status = "正在验证服务器连接…";
            break;
        case 3:
            m_status = "已连接 · 尚未登录";
            break;
        case 4:
            m_status = "正在注册账号…";
            break;
        case 5:
            m_status = "正在验证账号…";
            break;
        case 6:
            m_status = "已登录 · " + m_user;
            activity("账号已登录", m_user);
            QTimer::singleShot(0,this,[this]{if(m_online)requestProfile(m_user);});
            break;
        default:
            m_status = "连接状态未知";
        }
        emit sessionChanged();
        return;
    }
    if (type == "message") {
        applyRecord(event["value"].toObject());
        return;
    }
    if (type == "file") {
        applyFile(event["value"].toObject());
        return;
    }
    if (type == "group") {
        applyGroup(event["value"].toObject());
        emit conversationsChanged();
        return;
    }
    if (type == "contacts") {
        m_contacts = contactList(event["users"].toArray(), true);
        m_incoming = strings(event["incoming"]);
        m_outgoing = strings(event["outgoing"]);
        for (const auto& contact : m_contacts)
            ensurePeer(contact.username);
        for (auto it = m_pendingFriendRequests.begin(); it != m_pendingFriendRequests.end();)
            if (m_outgoing.contains(it.key()) || isFriend(it.key()))
                it = m_pendingFriendRequests.erase(it);
            else
                ++it;
        refreshPeerConversations();
        emit conversationsChanged();
        emit contactsChanged();
        return;
    }
    if (type == "strangers") {
        auto result = contactList(event["users"].toArray(), false);
        m_searchResults = result;
        m_searchNext = event["next"].toString();
        emit contactsChanged();
        return;
    }
    if (type == "friendResult") {
        const QString user = event["username"].toString();
        m_pendingFriendRequests.remove(user);
        if (!event["ok"].toBool())
            error(event["error"].toString());
        else {
            const QString state = event["state"].toString();
            if (state == "pending" && !m_outgoing.contains(user))
                m_outgoing.append(user);
            else if (state != "pending")
                m_outgoing.removeAll(user);
            activity("好友申请", user + " · " + state);
        }
        emit contactsChanged();
        return;
    }
    if (type == "presence") {
        for (auto& c : m_contacts)
            if (c.username == event["username"].toString())
                c.online = event["online"].toBool();
        refreshPeerConversations();
        emit contactsChanged();
        emit conversationsChanged();
        return;
    }
    if (type == "commandResult") {
        const QString rid = event["requestId"].toString();
        if (!event["accepted"].toBool()) {
            for (auto& m : m_messages)
                if (m.id == rid) {
                    m.state = SendState::Failed;
                    m.transportStatus = event["error"].toObject()["message"].toString();
                    emit messagesChanged(m.conversation);
                }
        }
        return;
    }
    if (type == "groupReport") {
        auto report = event["value"].toObject();
        const QString logical = report["logicalMessageId"].toString();
        if (!m_groupPending.isEmpty()) {
            QString rid = m_groupPending.takeFirst();
            for (auto& m : m_messages)
                if (m.id == rid) {
                    m.id = logical;
                    break;
                }
        }
        int failed = 0;
        for (auto member : report["members"].toArray()) {
            auto item = member.toObject();
            if (!item["error"].toString().isEmpty())
                ++failed;
            else
                m_memberLogical[item["message_id"].toString()] = logical;
        }
        for (auto& m : m_messages)
            if (m.id == logical) {
                m.transportStatus =
                    failed ? QString("%1 位成员未排队，可重试").arg(failed) : "已向各成员排队";
                m.state = failed ? SendState::Failed : SendState::Sending;
                emit messagesChanged(m.conversation);
            }
        return;
    }
    if (type == "messageStage") {
        auto stage = event["value"].toObject();
        const QString rawId = stage["messageId"].toString();
        if (m_memberLogical.contains(rawId))
            return;
        const int n = stage["stage"].toInt();
        QStringList labels = {"本地待发送",   "等待身份核对", "已保存到发送队列", "已提交传输",
                              "服务器已接收", "对端已签收",   "发送失败"};
        for (auto& m : m_messages)
            if (m.id == rawId) {
                m.transportStatus = labels.value(n, "状态未知");
                m.state = n == 6 ? SendState::Failed : n == 5 ? SendState::Stored : SendState::Sending;
                emit messagesChanged(m.conversation);
            }
        return;
    }
    if (type == "error") {
        const int code = event["code"].toInt();
        if (code != 5)
            error(event["message"].toString());
        return;
    }
}
} // namespace Messager
