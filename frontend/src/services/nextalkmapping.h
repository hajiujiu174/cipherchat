#pragma once
#include "core/types.h"
#include <QJsonObject>
namespace Messager::NextalkMapping {
// These are native runtime command/event DTOs, not server wire messages.
// 发送文本。
QJsonObject sendText(const QString& peer, const QString& text);
// 发送群聊。
QJsonObject sendGroup(qint64 groupId, const QString& text);
// 发送文件。
QJsonObject sendFile(const QString& peer, const QString& path);
// 返回nextalkmapping 模块使用的消息。
Message message(const QJsonObject& record, const QString& self);
// 返回nextalkmapping 模块使用的文件。
Transfer file(const QJsonObject& progress);
// 返回nextalkmapping 模块使用的投递标签。
QString deliveryLabel(int deliveryState);
} // namespace Messager::NextalkMapping
