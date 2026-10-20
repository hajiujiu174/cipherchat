#pragma once
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Messager {
enum class SendState { Stored, Sending, Failed };
enum class TransferState { Running, Paused, Completed, Failed, Cancelled };
struct Conversation {
    QString id, name, initials;
    QColor color;
    bool group = false;
    QString preview, time, subtitle;
    int unread = 0;
    QString peer;
    qint64 groupId = 0;
    QStringList members;
};
struct Message {
    QString id, conversation, sender, text, time;
    bool outgoing = false, file = false;
    SendState state = SendState::Stored;
    QString transportStatus; // Empty for mock data; real backend supplies its precise state label.
    qint64 timestamp = 0;
};
struct Transfer {
    QString id, name, meta, conversation;
    int progress = 0;
    TransferState state = TransferState::Running;
    bool simulated = true;
    bool outgoing = false;
    QString path;
    qint64 timestamp = 0;
    QString messageId;
};
struct Contact {
    QString username;
    bool online = false;
    bool friendContact = false;
};
struct Activity {
    QString title, text, conversation;
    bool viewed = false;
};
} // namespace Messager
