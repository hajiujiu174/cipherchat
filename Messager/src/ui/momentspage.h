#pragma once
#include "services/chatservice.h"
#include <QWidget>
class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;
namespace Messager {
class MomentsPage final : public QWidget {
    Q_OBJECT
  public:
    explicit MomentsPage(ChatService*,QWidget* parent=nullptr);
    void openFeed();
  signals:
    void profileRequested(const QString& user);
  private:
    void render();
    void load(bool older=false);
    ChatService* m_service;
    QLabel* m_status;
    QPushButton *m_refresh,*m_publish,*m_more;
    QVBoxLayout* m_posts;
    QScrollArea* m_scroll;
    bool m_loading=false;
};
}
