#pragma once
#include "core/types.h"
#include <QJsonObject>
namespace Messager::CipherChatMapping {
// These are native runtime command/event DTOs, not server wire messages.
QJsonObject sendText(const QString& peer, const QString& text);
QJsonObject sendGroup(qint64 groupId, const QString& text);
QJsonObject sendFile(const QString& peer, const QString& path);
Message message(const QJsonObject& record, const QString& self);
Transfer file(const QJsonObject& progress);
QString deliveryLabel(int deliveryState);
} // namespace Messager::CipherChatMapping
