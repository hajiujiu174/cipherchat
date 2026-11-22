#pragma once
#include "services/chatservice.h"
namespace Messager {
class AppController final : public QObject {
    Q_OBJECT
  public:
    explicit AppController(ChatService* service, QObject* parent = nullptr);
    QString selectedId() const {
        return m_selected;
    }
    ChatService* service() const {
        return m_service;
    }
    void selectConversation(const QString& id);
    void openDirectConversation(const QString& username);
    QString sendText(const QString& text, bool fail = false);
    void simulateIncoming();
    void attachFile(const QString& path);
  signals:
    void selectionChanged(const QString& id);

  private:
    ChatService* m_service;
    QString m_selected = "design";
};
} // namespace Messager
