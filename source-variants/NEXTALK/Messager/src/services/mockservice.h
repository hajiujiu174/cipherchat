#pragma once
#include "chatservice.h"
#include <QObject>
#include <QTimer>

namespace Messager {
// The sole data boundary. No sockets, HTTP, disk file transfer or authentication.
class MockService final : public ChatService {
    Q_OBJECT
  public:
    explicit MockService(QObject* parent = nullptr);
    const QVector<Conversation>& conversations() const {
        return m_conversations;
    }
    QVector<Message> messages(const QString& conversation) const;
    const QVector<Transfer>& transfers() const {
        return m_transfers;
    }
    Conversation conversation(const QString& id) const;
    QString send(const QString& conversation, const QString& text, bool fail = false);
    void retryMessage(const QString& id);
    void markViewed(const QString& conversation);
    void simulateIncoming(const QString& conversation);
    void addTransfer(const QString& name, qint64 bytes, const QString& conversation);
    void sendFile(const QString& path, const QString& conversation) override;
    void transferAction(const QString& id, const QString& action);
    void tickTransfers();

  private:
    void finishSend(const QString& id, bool fail);
    QVector<Conversation> m_conversations;
    QVector<Message> m_messages;
    QVector<Transfer> m_transfers;
    QTimer m_timer;
};
} // namespace Messager
