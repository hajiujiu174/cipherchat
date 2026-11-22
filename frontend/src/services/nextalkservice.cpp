#include "nextalkservice.h"
#include "nextalkjsonhelpers.h"
#include "features/games/gomokuwire.h"
#include "nextalkmapping.h"
#include "features/files/localfiles.h"
#include "features/files/imageimport.h"
#include <QDateTime>
#include <QTimer>
#include <QSet>
#include <QSettings>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QFile>
#include <QUuid>
#include <algorithm>
namespace Messager {
namespace {
// 返回nextalkservice 模块使用的联系人列表。
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
// 构造桌面联机服务，初始化后续操作需要的状态或依赖。
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
// 销毁桌面联机服务时释放它持有的资源。
NextalkService::~NextalkService() { saveViewState(); m_reconnectRequest={}; }
// 返回桌面联机服务使用的已查看状态键。
QString NextalkService::viewStateKey() const {
    if(m_user.isEmpty())return {};
    QFile certificate(m_config["caFile"].toString());QByteArray cert;
    if(certificate.open(QIODevice::ReadOnly))cert=certificate.readAll();
    const QJsonObject scope{{"host",m_config["host"]},{"port",m_config["port"]},
        {"serverName",m_config["serverName"]},{"user",m_user},
        {"certificate",QString::fromLatin1(QCryptographicHash::hash(cert,QCryptographicHash::Sha256).toHex())}};
    return "liveView/"+QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(scope).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
// 将已查看状态保存到本机设置，以便下次打开时恢复。
void NextalkService::saveViewState() {
    if(!m_viewStateLoaded)return;
    const auto key=viewStateKey();if(key.isEmpty())return;
    QJsonObject read;for(auto it=m_readBefore.cbegin();it!=m_readBefore.cend();++it)read[it.key()]=it.value();
    QJsonArray notices;for(const auto& a:m_activities)notices.append(QJsonObject{{"title",a.title},{"text",a.text},{"conversation",a.conversation},{"viewed",a.viewed}});
    QSettings s;s.setValue(key,QJsonDocument(QJsonObject{{"read",read},{"notices",notices}}).toJson(QJsonDocument::Compact));s.sync();
}
// 从本机设置读取并恢复已查看状态。
void NextalkService::loadViewState() {
    m_viewStateLoaded=true;
    m_readBefore.clear();m_activities.clear();QSettings s;
    const auto o=QJsonDocument::fromJson(s.value(viewStateKey()).toByteArray()).object();
    const auto read=o["read"].toObject();for(auto it=read.begin();it!=read.end();++it)m_readBefore[it.key()]=it.value().toInteger();
    for(const auto& value:o["notices"].toArray()){const auto a=value.toObject();m_activities.append({a["title"].toString(),a["text"].toString(),a["conversation"].toString(),a["viewed"].toBool()});if(m_activities.size()==150)break;}
    emit activitiesChanged();
}
// 在会话意外中断后按退避间隔重连，跳过已作废的定时任务。
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
// 重置视图。
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
// 要求在线。
bool NextalkService::requireOnline() {
    if (m_online)
        return true;
    error("请先登录 NEXTALK 后再操作");
    return false;
}
// 模拟收到的。
void NextalkService::simulateIncoming(const QString&) {
    error("真实模式不生成模拟消息");
}
// 标记活动通知已查看。
void NextalkService::markActivitiesViewed() {
    for (auto& a : m_activities)
        a.viewed = true;
    saveViewState();
    emit activitiesChanged();
}
// 更新桌面联机服务中的activity状态。
void NextalkService::activity(const QString& title, const QString& text, const QString& id) {
    m_activities.prepend({title, text, id, false});
    if (m_activities.size() > 150)
        m_activities.removeLast();
    saveViewState();
    emit activitiesChanged();
}
// 返回桌面联机服务的失败时的错误信息。
void NextalkService::error(const QString& message) {
    activity("操作提示", message);
    emit operationError(message);
}
// 按业务事件类型更新账号、会话、联系人、文件及桌面提示。
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
        m_incoming = NextalkJson::strings(event["incoming"]);
        m_outgoing = NextalkJson::strings(event["outgoing"]);
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
        QStringList labels = {"本地待发送",   "等待处理", "已保存到发送队列", "已提交传输",
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
