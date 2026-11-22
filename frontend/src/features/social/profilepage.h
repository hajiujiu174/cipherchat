#pragma once
#include "ui/components.h"
#include "services/chatservice.h"
class QLineEdit;
class QVBoxLayout;
class QLabel;
namespace Messager {
// 返回资料与社交使用的资料头像。
QPixmap profileAvatar(const QString& base64, int size);
// 添加动态操作列表。
void addMomentActions(QVBoxLayout*,ChatService*,const QJsonObject&,const QString&,QWidget*);
class ProfilePage final : public QWidget {
    Q_OBJECT
  public:
    // 构造个人资料页，初始化后续操作需要的状态或依赖。
    explicit ProfilePage(ChatService*, QWidget* parent=nullptr);
    // 切换个人资料页当前展示的账号，请求该账号的最新资料。
    void openUser(const QString& username);
    // 读取用户选择的头像文件并预览可保存的图片。
    bool chooseAvatarFile(const QString& path);
  signals:
    // 通知调用方已请求back操作。
    void backRequested();
    // 通知调用方已请求聊天操作。
    void chatRequested(const QString& user);
  private:
    // 刷新个人资料页。
    void refresh();
    // 更新操作列表。
    void updateActions();
    ChatService* m_service;
    QString m_user,m_avatar,m_publishKey,m_momentImage;
    bool m_dirty=false,m_saving=false,m_posting=false;
    Avatar* m_picture;
    QLabel *m_name,*m_account,*m_notice,*m_count;
    QLineEdit* m_nickname;
    QPlainTextEdit* m_text;
    QWidget *m_editArea,*m_compose;
    QPushButton *m_save,*m_publish,*m_more,*m_message;
    QVBoxLayout* m_posts;
    qint64 m_next=0;
};
}
