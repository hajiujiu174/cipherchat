#pragma once
#include "chatservice.h"
#include <QObject>
#include <QTimer>

namespace Messager {
// The sole data boundary. No sockets, HTTP, disk file transfer or authentication.
class MockService final : public ChatService {
    Q_OBJECT
  public:
    // 构造桌面演示数据服务，初始化后续操作需要的状态或依赖。
    explicit MockService(QObject* parent = nullptr);
    // 返回桌面演示数据服务使用的会话列表。
    const QVector<Conversation>& conversations() const {
        return m_conversations;
    }
    // 供界面读取当前的消息。
    QVector<Message> messages(const QString& conversation) const;
    // 返回桌面演示数据服务使用的传输任务列表。
    const QVector<Transfer>& transfers() const {
        return m_transfers;
    }
    // 返回桌面演示数据服务使用的会话。
    Conversation conversation(const QString& id) const;
    // 发送桌面演示数据服务。
    QString send(const QString& conversation, const QString& text, bool fail = false);
    // 重试消息。
    void retryMessage(const QString& id);
    // 标记已查看。
    void markViewed(const QString& conversation);
    // 模拟收到的。
    void simulateIncoming(const QString& conversation);
    // 添加传输。
    void addTransfer(const QString& name, qint64 bytes, const QString& conversation);
    // 发送文件。
    void sendFile(const QString& path, const QString& conversation) override;
    // 传输操作。
    void transferAction(const QString& id, const QString& action);
    // 推进一轮传输任务列表。
    void tickTransfers();

  private:
    // 完成Send。
    void finishSend(const QString& id, bool fail);
    QVector<Conversation> m_conversations;
    QVector<Message> m_messages;
    QVector<Transfer> m_transfers;
    QTimer m_timer;
};
} // namespace Messager
