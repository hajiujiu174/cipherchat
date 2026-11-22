#include "services/nextalkservice.h"
#include "services/nextalkmapping.h"
#include "features/files/localfiles.h"
#include "features/files/imageimport.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <QDateTime>

namespace Messager {

// 根据会话类型和当前好友或群成员关系判断是否开放文件发送。
bool NextalkService::canSendFiles(const QString& id) const {
    const auto c = conversation(id);
    return c.group?c.members.contains(m_user):ChatService::canSendFiles(id) && !c.peer.isEmpty() && isFriend(c.peer);
}

// 添加传输。
void NextalkService::addTransfer(const QString&, qint64, const QString&) {
    error("请通过文件选择器发送真实文件");
}

// 检查当前会话及本地文件，提交私聊附件或按群成员分别提交文件任务。
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

// 将文件卡片的暂停、继续或取消操作映射为运行时命令。
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

// 将运行时文件进度投影为桌面的传输条目和聊天文件气泡。
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

} // namespace Messager
