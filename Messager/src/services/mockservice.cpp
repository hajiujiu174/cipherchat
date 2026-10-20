#include "mockservice.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace Messager {
void MockService::sendFile(const QString& path, const QString& conversationId) {
    QFileInfo file(path);
    if (file.isFile())
        addTransfer(file.fileName(), file.size(), conversationId);
}
MockService::MockService(QObject* parent) : ChatService(parent) {
    QFile file(":/resources/demo.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing demo data resource");
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& value : root["conversations"].toArray()) {
        const auto o = value.toObject();
        m_conversations.push_back({o["id"].toString(), o["name"].toString(), o["initials"].toString(),
                                   QColor(o["color"].toString()), o["group"].toBool(),
                                   o["preview"].toString(), o["time"].toString(), o["subtitle"].toString(),
                                   o["unread"].toInt()});
    }
    for (const auto& value : root["messages"].toArray()) {
        const auto o = value.toObject();
        m_messages.push_back({o["id"].toString(), o["conversation"].toString(), o["sender"].toString(),
                              o["text"].toString(), o["time"].toString(), o["outgoing"].toBool(),
                              o["file"].toBool(), SendState::Stored});
    }
    m_transfers = {
        {"t1", "品牌视觉素材.zip", "24.8 MB · 来自 产品设计讨论", "design", 42, TransferState::Running},
        {"t2", "Messager 交互说明.pdf", "2.4 MB · 来自 周屿", "design", 100, TransferState::Completed},
        {"t3", "桌面端原型.fig", "8.6 MB · 发往 林予", "lin", 31, TransferState::Failed},
        {"t4", "图标资源.svg", "128 KB · 来自 许知夏", "xu", 64, TransferState::Paused}};
    m_transfers[1].messageId = "m4";
    m_timer.setInterval(700);
    connect(&m_timer, &QTimer::timeout, this, &MockService::tickTransfers);
    m_timer.start();
}
Conversation MockService::conversation(const QString& id) const {
    for (const auto& c : m_conversations)
        if (c.id == id)
            return c;
    return {};
}
QVector<Message> MockService::messages(const QString& id) const {
    QVector<Message> result;
    for (const auto& m : m_messages)
        if (m.conversation == id)
            result.push_back(m);
    return result;
}
QString MockService::send(const QString& conversationId, const QString& text, bool fail) {
    if (text.trimmed().isEmpty() || conversation(conversationId).id.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString time = QTime::currentTime().toString("HH:mm");
    m_messages.push_back({id, conversationId, "我", text, time, true, false, SendState::Sending});
    for (auto& c : m_conversations)
        if (c.id == conversationId) {
            c.preview = "我：" + text.simplified();
            c.time = time;
        }
    emit messagesChanged(conversationId);
    emit conversationsChanged();
    QTimer::singleShot(850, this, [this, id, fail] { finishSend(id, fail); });
    return id;
}
void MockService::finishSend(const QString& id, bool fail) {
    for (auto& m : m_messages)
        if (m.id == id) {
            m.state = fail ? SendState::Failed : SendState::Stored;
            emit messagesChanged(m.conversation);
            return;
        }
}
void MockService::retryMessage(const QString& id) {
    for (auto& m : m_messages)
        if (m.id == id && m.state == SendState::Failed) {
            m.state = SendState::Sending;
            emit messagesChanged(m.conversation);
            QTimer::singleShot(850, this, [this, id] { finishSend(id, false); });
            return;
        }
}
void MockService::markViewed(const QString& id) {
    for (auto& c : m_conversations)
        if (c.id == id && c.unread) {
            c.unread = 0;
            emit conversationsChanged();
            return;
        }
}
void MockService::simulateIncoming(const QString& id) {
    const auto c = conversation(id);
    if (c.id.isEmpty())
        return;
    QString text = "这是一条模拟新消息。你可以继续调整窗口大小，看看布局的变化。";
    m_messages.push_back({QUuid::createUuid().toString(), id, c.group ? "林予" : c.name, text,
                          QTime::currentTime().toString("HH:mm"), false, false, SendState::Stored});
    for (auto& entry : m_conversations)
        if (entry.id == id) {
            entry.preview = text;
            entry.time = QTime::currentTime().toString("HH:mm");
            ++entry.unread;
        }
    emit conversationsChanged();
    emit messagesChanged(id);
}
void MockService::addTransfer(const QString& name, qint64 bytes, const QString& conversationId) {
    if (name.isEmpty())
        return;
    const QString size = bytes > 1024 * 1024 ? QString::number(bytes / 1048576.0, 'f', 1) + " MB"
                                             : QString::number(qMax<qint64>(1, bytes / 1024)) + " KB";
    Transfer transfer{QUuid::createUuid().toString(), name,
                      size + " · 模拟发送至 " + conversation(conversationId).name, conversationId, 0,
                      TransferState::Running};
    transfer.messageId = "file:" + transfer.id;
    m_transfers.prepend(transfer);
    m_messages.push_back({transfer.messageId, conversationId, "我", name,
                          QTime::currentTime().toString("HH:mm"), true, true, SendState::Stored});
    emit transfersChanged();
    emit messagesChanged(conversationId);
}
void MockService::transferAction(const QString& id, const QString& action) {
    for (auto& t : m_transfers)
        if (t.id == id) {
            if (action == "cancel" && (t.state == TransferState::Running || t.state == TransferState::Paused))
                t.state = TransferState::Cancelled;
            else if (action == "pause" && t.state == TransferState::Running)
                t.state = TransferState::Paused;
            else if (action == "resume" && t.state == TransferState::Paused)
                t.state = TransferState::Running;
            else if (action == "retry" &&
                     (t.state == TransferState::Failed || t.state == TransferState::Cancelled)) {
                t.progress = 0;
                t.state = TransferState::Running;
            }
            emit transfersChanged();
            return;
        }
}
void MockService::tickTransfers() {
    bool changed = false;
    for (auto& t : m_transfers)
        if (t.state == TransferState::Running) {
            t.progress = qMin(100, t.progress + 2);
            if (t.progress == 100)
                t.state = TransferState::Completed;
            changed = true;
        }
    if (changed)
        emit transfersChanged();
}
} // namespace Messager
