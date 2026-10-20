#include "cipherchatmapping.h"
#include <QDateTime>
namespace Messager::CipherChatMapping {
QJsonObject sendText(const QString& peer, const QString& text) {
    return {{"command", "sendText"}, {"peer", peer}, {"text", text}};
}
QJsonObject sendGroup(qint64 id, const QString& text) {
    return {{"command", "sendGroup"}, {"groupId", id}, {"text", text}};
}
QJsonObject sendFile(const QString& peer, const QString& path) {
    return {{"command", "sendFile"}, {"peer", peer}, {"path", path}};
}
QString deliveryLabel(int state) {
    switch (state) {
    case 0:
        return "本地待发送";
    case 1:
        return "服务器已接收";
    case 2:
        return "对端已签收";
    case 3:
        return "发送失败";
    default:
        return "状态未知";
    }
}
Message message(const QJsonObject& o, const QString& self) {
    Message m;
    m.id = o["messageId"].toString();
    m.sender = o["from"].toString();
    m.outgoing = m.sender == self;
    auto group = o["groupId"].toInteger();
    m.conversation =
        group ? "group:" + QString::number(group) : "peer:" + (m.outgoing ? o["to"].toString() : m.sender);
    m.text = o["text"].toString();
    auto timestamp = o["timestamp"].toInteger();
    m.time = timestamp > 0 ? QDateTime::fromMSecsSinceEpoch(timestamp).toLocalTime().toString("HH:mm")
                           : "时间未记录";
    const int state = o["state"].toInt(-1);
    m.state = state == 3 ? SendState::Failed : state == 0 ? SendState::Sending : SendState::Stored;
    m.transportStatus = deliveryLabel(state);
    m.timestamp = o["timestamp"].toInteger();
    return m;
}
Transfer file(const QJsonObject& o) {
    Transfer t;
    t.id = o["fileId"].toString();
    t.messageId = "file:" + t.id;
    t.name = o["name"].toString();
    t.conversation = "peer:" + o["peer"].toString();
    auto bytes = o["authenticatedBytes"].toInteger();
    auto total = o["totalBytes"].toInteger();
    t.progress = total > 0 ? int(qBound(qint64(0), bytes * 100 / total, qint64(100))) : 0;
    t.state = o["cancelled"].toBool()  ? TransferState::Cancelled
              : o["complete"].toBool() ? TransferState::Completed
                                       : TransferState::Running;
    if (t.state == TransferState::Completed)
        t.progress = 100;
    t.meta = t.state == TransferState::Completed ? QString("%1 字节 · 后端确认完成").arg(total)
                                                 : QString("%1 / %2 字节 · 已验证进度").arg(bytes).arg(total);
    return t;
}
} // namespace Messager::CipherChatMapping
