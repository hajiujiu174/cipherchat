#pragma once
#include "services/chatservice.h"
namespace Messager {
class AppController final : public QObject {
    Q_OBJECT
  public:
    // 构造桌面应用控制器，初始化后续操作需要的状态或依赖。
    explicit AppController(ChatService* service, QObject* parent = nullptr);
    // 返回桌面应用控制器使用的选中的ID。
    QString selectedId() const {
        return m_selected;
    }
    // 返回控制器使用的聊天服务对象。
    ChatService* service() const {
        return m_service;
    }
    // 切换到有效的会话，更新查看状态并通知界面刷新选中项。
    void selectConversation(const QString& id);
    // 打开私聊会话。
    void openDirectConversation(const QString& username);
    // 把正文和发送选项交给当前选中会话的聊天服务。
    QString sendText(const QString& text, bool fail = false);
    // 模拟收到的。
    void simulateIncoming();
    // 把已选择的附件路径交给当前会话的文件发送接口。
    void attachFile(const QString& path);
  signals:
    // 通知监听方选中项发生变化，供界面或调用方刷新。
    void selectionChanged(const QString& id);

  private:
    ChatService* m_service;
    QString m_selected = "design";
};
} // namespace Messager
