#pragma once
#include "components.h"
#include "services/chatservice.h"
class QLineEdit;
class QVBoxLayout;
class QLabel;
namespace Messager {
QPixmap profileAvatar(const QString& base64, int size);
void addMomentActions(QVBoxLayout*,ChatService*,const QJsonObject&,const QString&,QWidget*);
class ProfilePage final : public QWidget {
    Q_OBJECT
  public:
    explicit ProfilePage(ChatService*, QWidget* parent=nullptr);
    void openUser(const QString& username);
    bool chooseAvatarFile(const QString& path);
  signals:
    void backRequested();
    void chatRequested(const QString& user);
  private:
    void refresh();
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
