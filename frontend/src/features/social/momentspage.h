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
    // 构造动态信息流页面，初始化后续操作需要的状态或依赖。
    explicit MomentsPage(ChatService*,QWidget* parent=nullptr);
    // 打开信息流。
    void openFeed();
  signals:
    // 通知调用方已请求资料操作。
    void profileRequested(const QString& user);
  private:
    // 根据当前动态列表内容重建信息流卡片与操作入口。
    void render();
    // 加载动态信息流页面。
    void load(bool older=false);
    ChatService* m_service;
    QLabel* m_status;
    QPushButton *m_refresh,*m_publish,*m_more;
    QVBoxLayout* m_posts;
    QScrollArea* m_scroll;
    bool m_loading=false;
};
}
