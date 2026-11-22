#include "services/nextalkservice.h"
#include "services/nextalkmapping.h"
#include <QJsonArray>
#include <algorithm>
#include "services/nextalkjsonhelpers.h"

namespace Messager {

// 创建群聊。
void NextalkService::createGroup(const QString& name, const QStringList& members) {
    if (requireOnline())
        m_gateway.post(
            {{"command", "createGroup"}, {"name", name}, {"members", QJsonArray::fromStringList(members)}});
}

// 应用群聊。
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
    c.members = NextalkJson::strings(o["members"]);
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

} // namespace Messager
