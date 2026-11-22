#include "services/nextalkservice.h"
#include "services/nextalkmapping.h"
#include "features/games/gomokuwire.h"

#include <QDateTime>
#include <QJsonArray>
#include <QUuid>
#include <algorithm>

namespace Messager {
namespace {

// 返回聊天消息使用的当前时间文本。
QString nowText() {
    return QTime::currentTime().toString("HH:mm");
}

const QString controlPrefix="NEXTALK:EVENT:1\n";

} // namespace

// 在本地会话列表中确保存在这位对端的私聊条目。
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

// 从当前对端列表更新私聊会话的显示信息。
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

// 建立或选择指定对端的本地私聊会话条目。
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

// 返回桌面联机服务使用的会话。
Conversation NextalkService::conversation(const QString& id) const {
    for (const auto& c : m_conversations)
        if (c.id == id)
            return c;
    return {};
}

// 返回指定会话的本地记录，并在展示时应用编辑、撤回和已读控制消息。
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

// 为自己发送的文字消息生成编辑或撤回事件，交由原会话发送。
void NextalkService::changeMessage(const QString& id,const QString& text,bool retract) {
    for(const auto& stored:m_messages)if(stored.id==id&&stored.outgoing&&!stored.file&&!stored.text.startsWith(controlPrefix)) {
        const QJsonObject value{{"op",retract?"retract":"edit"},{"target",id},{"text",text}};
        send(stored.conversation,controlPrefix+QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));return;
    }
    error("只能修改自己发送的文字消息");
}

// 为当前会话生成请求 ID，通过网关提交单聊或群聊发送，并先显示待发送气泡。
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

// 复用原消息标识提交私聊重试，群聊按逻辑消息标识继续原任务。
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

// 更新会话的本地阅读位置和未读计数，并在条件满足时提交已读事件。
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

// 把框架返回的消息映射为桌面记录，按消息 ID 去重并聚合群聊签收。
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

} // namespace Messager
