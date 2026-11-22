#include "nextalkservice.h"
#include "gomokuwire.h"
#include "nextalkmapping.h"
#include "localfiles.h"
#include "imageimport.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QTimer>
#include <QSet>
#include <QSettings>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QFile>
#include <QUrl>
#include <QUuid>
#include <algorithm>
namespace Messager {
namespace {
const QString controlPrefix="NEXTALK:EVENT:1\n";
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
NextalkService::NextalkService(QJsonObject config, QObject* parent)
    : ChatService(parent), m_config(config) {
    m_config.remove("password");
    connect(&m_gateway, &NextalkGateway::eventReceived, this, &NextalkService::handle);
    connect(&m_gateway, &NextalkGateway::commandRejected, this, &NextalkService::error);
    connect(&m_udp,&GameUdp::packet,this,[this](const QString& peer,const QJsonObject& data){
        if(online()&&isFriend(peer))emit gamePacket(peer,data);
    });
    auto* profileTimer = new QTimer(this);
    profileTimer->setInterval(1000);
    connect(profileTimer, &QTimer::timeout, this, &NextalkService::refreshProfileSummaries);
    profileTimer->start();
    auto* ticketTimer=new QTimer(this);ticketTimer->setInterval(240000);connect(ticketTimer,&QTimer::timeout,this,[this]{if(m_online)socialAction({{"op","udp_ticket"}});});ticketTimer->start();
}
NextalkService::~NextalkService() { saveViewState(); m_reconnectRequest={}; }
QString NextalkService::viewStateKey() const {
    if(m_user.isEmpty())return {};
    QFile certificate(m_config["caFile"].toString());QByteArray cert;
    if(certificate.open(QIODevice::ReadOnly))cert=certificate.readAll();
    const QJsonObject scope{{"host",m_config["host"]},{"port",m_config["port"]},
        {"serverName",m_config["serverName"]},{"user",m_user},
        {"certificate",QString::fromLatin1(QCryptographicHash::hash(cert,QCryptographicHash::Sha256).toHex())}};
    return "liveView/"+QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(scope).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
void NextalkService::saveViewState() {
    if(!m_viewStateLoaded)return;
    const auto key=viewStateKey();if(key.isEmpty())return;
    QJsonObject read;for(auto it=m_readBefore.cbegin();it!=m_readBefore.cend();++it)read[it.key()]=it.value();
    QJsonArray notices;for(const auto& a:m_activities)notices.append(QJsonObject{{"title",a.title},{"text",a.text},{"conversation",a.conversation},{"viewed",a.viewed}});
    QSettings s;s.setValue(key,QJsonDocument(QJsonObject{{"read",read},{"notices",notices}}).toJson(QJsonDocument::Compact));s.sync();
}
void NextalkService::loadViewState() {
    m_viewStateLoaded=true;
    m_readBefore.clear();m_activities.clear();QSettings s;
    const auto o=QJsonDocument::fromJson(s.value(viewStateKey()).toByteArray()).object();
    const auto read=o["read"].toObject();for(auto it=read.begin();it!=read.end();++it)m_readBefore[it.key()]=it.value().toInteger();
    for(const auto& value:o["notices"].toArray()){const auto a=value.toObject();m_activities.append({a["title"].toString(),a["text"].toString(),a["conversation"].toString(),a["viewed"].toBool()});if(m_activities.size()==150)break;}
    emit activitiesChanged();
}
void NextalkService::scheduleReconnect() {
    if(!m_hadOnline || m_reconnectRequest.isEmpty())return;
    const auto generation=++m_reconnectGeneration;
    const int delay=qMin(30000,1000*(1<<qMin(m_reconnectAttempt++,5)));
    m_status=QString("连接中断 · %1 秒后自动重连").arg(delay/1000);
    QTimer::singleShot(delay,this,[this,generation]{
        if(generation!=m_reconnectGeneration || m_online || m_reconnectRequest.isEmpty())return;
        m_gateway.post(m_reconnectRequest);m_status="正在自动重连…";emit sessionChanged();
    });
}
void NextalkService::resetView() {
    m_viewStateLoaded=false;
    m_udp.stop();
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
void NextalkService::requestRegistrationCode(const QJsonObject& request) {
    auto command = request;
    command["requestCodeOnly"] = true; command["registerNew"] = false; command["password"] = "";
    authenticate(command);
}
void NextalkService::resetPassword(const QJsonObject& request) {
    auto command=request; command["passwordReset"]=true; command["registerNew"]=false; command["requestCodeOnly"]=false;
    authenticate(command);
}
void NextalkService::authenticate(const QJsonObject& request) {
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
    saveViewState();++m_reconnectGeneration;m_hadOnline=false;m_reconnectAttempt=0;m_reconnectRequest={};
    if(!request["passwordReset"].toBool()&&!request["requestCodeOnly"].toBool()&&!request["emailLogin"].toBool()){
        m_reconnectRequest=command;m_reconnectRequest["registerNew"]=false;m_reconnectRequest.remove("emailCode");m_reconnectRequest.remove("email");
    }
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
void NextalkService::disconnectAccount() {
    saveViewState();++m_reconnectGeneration;m_hadOnline=false;m_reconnectRequest={};
    m_gateway.post({{"command", "disconnect"}});
    m_online = false;
    resetView();
    m_status = "已退出登录";
    emit sessionChanged();
}
bool NextalkService::requireOnline() {
    if (m_online)
        return true;
    error("请先登录 NEXTALK 后再操作");
    return false;
}
void NextalkService::ensurePeer(const QString& peer) {
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
void NextalkService::refreshPeerConversations() {
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
QString NextalkService::openDirectConversation(const QString& username) {
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
bool NextalkService::canSendFiles(const QString& id) const {
    const auto c = conversation(id);
    return c.group?c.members.contains(m_user):ChatService::canSendFiles(id) && !c.peer.isEmpty() && isFriend(c.peer);
}
Conversation NextalkService::conversation(const QString& id) const {
    for (const auto& c : m_conversations)
        if (c.id == id)
            return c;
    return {};
}
QVector<Message> NextalkService::messages(const QString& id) const {
    QVector<Message> r;
    for (const auto& m : m_messages)
        if (m.conversation == id && !m.text.startsWith(controlPrefix))
            r << m;
    std::stable_sort(r.begin(), r.end(),
                     [](const Message& a, const Message& b) { return a.timestamp < b.timestamp; });
    auto controls=m_messages;std::stable_sort(controls.begin(),controls.end(),[](const Message& a,const Message& b){return a.timestamp<b.timestamp;});
    for(const auto& control:controls)if(control.conversation==id&&control.text.startsWith(controlPrefix)) {
        const auto value=QJsonDocument::fromJson(control.text.mid(controlPrefix.size()).toUtf8()).object();
        for(auto& message:r)if((message.id==value["target"].toString()||(value["op"]=="read"&&message.timestamp<=value["before"].toInteger()))&&!message.file){
            if(value["op"]=="read"&&message.outgoing&&!control.outgoing){message.read=true;message.transportStatus="已读 · "+control.sender;}
            if(control.sender!=message.sender||message.retracted)continue;
            if(value["op"]=="retract"){message.text="此消息已撤回";message.retracted=true;}
            if(value["op"]=="edit"&&!value["text"].toString().trimmed().isEmpty()){message.text=value["text"].toString();message.edited=true;}
        }
    }
    return r;
}
void NextalkService::changeMessage(const QString& id,const QString& text,bool retract) {
    for(const auto& stored:m_messages)if(stored.id==id&&stored.outgoing&&!stored.file&&!stored.text.startsWith(controlPrefix)) {
        const QJsonObject value{{"op",retract?"retract":"edit"},{"target",id},{"text",text}};
        send(stored.conversation,controlPrefix+QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));return;
    }
    error("只能修改自己发送的文字消息");
}
QString NextalkService::send(const QString& id, const QString& text, bool) {
    if (!requireOnline() || text.trimmed().isEmpty())
        return {};
    const auto c = conversation(id);
    if (c.id.isEmpty()) {
        error("请选择联系人或群聊");
        return {};
    }
    const QString rid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto command =
        c.group ? NextalkMapping::sendGroup(c.groupId, text) : NextalkMapping::sendText(c.peer, text);
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
    const bool gameControl=GomokuWire::isControl(text)||text.startsWith(controlPrefix);
    const auto gameEnvelope=GomokuWire::decode(text);
    if(!gameControl)for (auto& entry : m_conversations)
        if (entry.id == id) {
            entry.lastActivity = m.timestamp;
            entry.preview = gameEnvelope["type"]=="invite"
                ? GomokuWire::title(GomokuWire::gameId(gameEnvelope))+"邀请 · 点击加入房间"
                : "我：" + text.simplified();
            entry.time = m.time;
        }
    if(!gameControl)emit conversationsChanged();
    emit messagesChanged(id);
    return rid;
}
void NextalkService::retryMessage(const QString& id) {
    if (!requireOnline())
        return;
    if (m_records.contains(id)) {
        auto record = m_records.value(id);
        auto command = NextalkMapping::sendText(record["to"].toString(), record["text"].toString());
        command["messageId"] = id;
        m_gateway.post(command, id);
    } else
        m_gateway.post({{"command", "retryGroup"}, {"logicalId", id}});
}
void NextalkService::markViewed(const QString& id) {
    m_viewing = id;
    m_readBefore[id]=QDateTime::currentMSecsSinceEpoch();saveViewState();
    bool needsReceipt=false;for(const auto& m:messages(id))needsReceipt|=!m.outgoing&&!m.file&&m.timestamp>m_receiptsSent.value(id);
    if(m_online&&needsReceipt){const QJsonObject value{{"op","read"},{"before",QDateTime::currentMSecsSinceEpoch()}};send(id,controlPrefix+QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));}
    m_receiptsSent[id]=QDateTime::currentMSecsSinceEpoch();
    for (auto& c : m_conversations)
        if (c.id == id && c.unread) {
            c.unread = 0;
            emit conversationsChanged();
            break;
        }
}
void NextalkService::simulateIncoming(const QString&) {
    error("真实模式不生成模拟消息");
}
void NextalkService::addTransfer(const QString&, qint64, const QString&) {
    error("请通过文件选择器发送真实文件");
}
void NextalkService::sendFile(const QString& path, const QString& id) {
    if (!requireOnline())
        return;
    auto c = conversation(id);
    if (!c.group && c.peer.isEmpty()) {
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
    if(c.group){for(const auto& member:c.members)if(member!=m_user){auto command=NextalkMapping::sendFile(member,path);command["groupId"]=c.groupId;m_gateway.post(command,QUuid::createUuid().toString(QUuid::WithoutBraces));}return;}
    m_gateway.post(NextalkMapping::sendFile(c.peer, path),
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
}
void NextalkService::transferAction(const QString& id, const QString& action) {
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
            else if (action == "pause" && t.state == TransferState::Running)
                m_gateway.post({{"command", "pauseFile"}, {"fileId", id}});
            else if (action == "cancel")
                m_gateway.post({{"command", "cancelFile"}, {"fileId", id}});
            else
                error("此状态不支持该操作；已取消文件需要重新选择发送");
            return;
        }
}
void NextalkService::searchUsers(const QString& query, const QString& after) {
    if (requireOnline())
        m_gateway.post({{"command", "searchUsers"}, {"query", query}, {"after", after}});
}
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
void NextalkService::respondFriend(const QString& user, bool accept) {
    if (requireOnline())
        m_gateway.post({{"command", "respondFriend"}, {"username", user}, {"accept", accept}});
}
void NextalkService::createGroup(const QString& name, const QStringList& members) {
    if (requireOnline())
        m_gateway.post(
            {{"command", "createGroup"}, {"name", name}, {"members", QJsonArray::fromStringList(members)}});
}
void NextalkService::markActivitiesViewed() {
    for (auto& a : m_activities)
        a.viewed = true;
    saveViewState();
    emit activitiesChanged();
}
void NextalkService::activity(const QString& title, const QString& text, const QString& id) {
    m_activities.prepend({title, text, id, false});
    if (m_activities.size() > 150)
        m_activities.removeLast();
    saveViewState();
    emit activitiesChanged();
}
void NextalkService::error(const QString& message) {
    activity("操作提示", message);
    emit operationError(message);
}
void NextalkService::applyGroup(const QJsonObject& o) {
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
    c.subtitle = c.members.isEmpty()?"已离开或群已解散":QString::number(c.members.size()) + " 位成员";
    for (auto& old : m_conversations)
        if (old.id == id) {
            c.preview = old.preview;
            c.time = old.time;
            c.lastActivity = old.lastActivity;
            c.unread = old.unread;
            old = c;
            return;
        }
    m_conversations << c;
}
void NextalkService::applyRecord(const QJsonObject& o, bool restored) {
    Message m = NextalkMapping::message(o, m_user);
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
    const bool gameControl=GomokuWire::isControl(m.text)||m.text.startsWith(controlPrefix);
    if(!gameControl)for (auto& c : m_conversations)
        if (c.id == m.conversation) {
            if (m.timestamp >= c.lastActivity) {
                c.lastActivity = m.timestamp;
                c.preview = (m.outgoing ? "我：" : m.sender + "：") + m.text.simplified();
                if(GomokuWire::decode(m.text)["type"]=="invite")c.preview=GomokuWire::title(GomokuWire::gameId(GomokuWire::decode(m.text)))+"邀请 · 点击加入房间";
                c.time = m.time;
            }
            if (!exists && !m.outgoing && m_viewing != c.id && m.timestamp>m_readBefore.value(c.id))
                ++c.unread;
        }
    if(!m.outgoing && m_viewing==m.conversation){m_readBefore[m.conversation]=qMax(QDateTime::currentMSecsSinceEpoch(),m.timestamp);saveViewState();}
    if (!gameControl && !exists && !restored && !m.outgoing)
        activity("收到新消息", m.sender + " 发来一条消息", m.conversation);
    if(!gameControl)emit conversationsChanged();
    emit messagesChanged(m.conversation);
}
void NextalkService::applyFile(const QJsonObject& o, bool restored) {
    auto t = NextalkMapping::file(o);
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
    if(t.state==TransferState::Completed&&!importImage(t.path).isNull())m.imagePath=t.path;
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
    for (auto& c : m_conversations)
        if (c.id == m.conversation && m.timestamp >= c.lastActivity) {
            c.lastActivity = m.timestamp;
            c.preview = (m.outgoing ? QStringLiteral("我：") : m.sender + "：") + "[文件] " + m.text;
            c.time = m.time;
        }
    if (!exists && !restored)
        activity("文件传输", t.name, t.conversation);
    emit conversationsChanged();
    emit transfersChanged();
    emit messagesChanged(t.conversation);
}
void NextalkService::updateProfileSummary(const QString& user,const QJsonObject& value) {
    const QJsonObject summary{{"nickname",value["nickname"]},{"avatar",value["avatar"]}};
    if(m_profileSummaries.value(user)==summary)return;
    m_profileSummaries[user]=summary;
    emit profileSummaryChanged(user);
}
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
void NextalkService::requestProfile(const QString& user,qint64 before) {
    if(!requireOnline())return;
    if(!before)++m_profileGenerations[user];
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","get"},{"username",user},{"before",before},{"view_id",QString::number(m_profileGenerations[user])}}}});
}
void NextalkService::requestMomentsFeed(qint64 beforeMs,qint64 beforeId) {
    if(!online()){emit momentsFeedFinished(false,"请登录后查看朋友圈");return;}
    if(!beforeId)++m_feedGeneration;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","feed"},{"before_ms",beforeMs},{"before_id",beforeId},{"view_id",QString::number(m_feedGeneration)}}}});
}
void NextalkService::saveProfile(const QString& nickname,const QString& avatar) {
    if(!requireOnline())return;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","save"},{"nickname",nickname},{"avatar",avatar}}}});
}
void NextalkService::publishMoment(const QString& text,const QString& key) {
    if(!requireOnline())return;
    m_gateway.post({{"command","social"},{"value",QJsonObject{{"op","publish"},{"text",text},{"request_id",key}}}});
}
void NextalkService::socialAction(const QJsonObject& value) {
    if(requireOnline())m_gateway.post({{"command","social"},{"value",value}});
}
bool NextalkService::sendGamePacket(const QString& peer,const QJsonObject& packet) {
    if(!gameRelaySupported() || !isFriend(peer))return false;
    auto data=packet;
    const bool actionGame=data["game"]=="tanktrouble"||data["game"]=="badminton";
    if(actionGame){
        data["udp"]=m_udp.capable()?1:0;
        if(data["net"].toInt()==2&&(data["type"]=="input"||data["type"]=="state")&&m_udp.send(peer,data))return true;
    }
    return m_gateway.post({{"command","gameRelay"},{"value",QJsonObject{{"to",peer},{"data",data}}}});
}
void NextalkService::handle(const QJsonObject& event) {
    const QString type = event["type"].toString();
    if(type=="gameRelay") {
        const auto value=event["value"].toObject();
        if(value.contains("supported")) { m_gameRelay=value["supported"].toBool();emit gameRelayChanged(); }
        else if(online() && isFriend(value["from"].toString())) {
            const auto peer=value["from"].toString();const auto data=value["data"].toObject();
            if(data["game"]=="tanktrouble"||data["game"]=="badminton")m_udp.advertise(peer,data["udp"].toInt()==1);
            emit gamePacket(peer,data);
        }
        return;
    }
    if(type=="social") {
        auto value=event["value"].toObject();const auto op=value["op"].toString(),user=value["username"].toString();
        const bool ok=value["ok"].toBool();
        emit socialResult(value);
        if(op=="call")return;
        if(op=="groups")return;
        if(op=="udp_ticket"){
            if(ok&&m_online){const auto host=m_config["gameUdpHost"].toString(m_config["host"].toString());const int port=m_config["gameUdpPort"].toInt(host=="8.160.172.25"?55221:0);if(port>0&&port<=65535)m_udp.start(host,quint16(port),m_user,value);}
            return;
        }
        if(op.startsWith("group_")||op.startsWith("friend_")||op=="block"||op=="unblock"||op=="blocked"||op.startsWith("app_")) {
            if(!ok)error(value["error"].toString()=="forbidden"?"没有权限执行此操作":"操作未完成："+value["error"].toString());
            else if(!op.startsWith("app_")&&op!="blocked")activity("操作完成",op);
            return;
        }
        if(op.startsWith("moment_")){if(!ok)error("动态操作未完成："+value["error"].toString());else{requestProfile(user);requestMomentsFeed();}return;}
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
        loadViewState();
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
        if(m_online){
            const auto host=m_config["gameUdpHost"].toString(m_config["host"].toString());
            const int port=m_config["gameUdpPort"].toInt(host=="8.160.172.25"?55221:0);
            if(port>0&&port<=65535)socialAction({{"op","udp_ticket"}});
        } else m_udp.stop();
        if (!m_online && !m_pendingFriendRequests.isEmpty()) {
            m_pendingFriendRequests.clear();
            emit contactsChanged();
        }
        switch (phase) {
        case 0:
            m_status = "连接已断开";
            saveViewState();scheduleReconnect();
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
            socialAction({{"op","groups"}});
            m_hadOnline=true;m_reconnectAttempt=0;++m_reconnectGeneration;
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
        if(code==4||event["message"].toString().contains("kicked")||event["message"].toString().contains("password_reset")){++m_reconnectGeneration;m_reconnectRequest={};m_hadOnline=false;}
        if (code != 5)
            error(event["message"].toString());
        return;
    }
}
} // namespace Messager
