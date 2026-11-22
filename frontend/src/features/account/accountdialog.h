#pragma once
#include "services/chatservice.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
class QLineEdit;
class QLabel;
class QCheckBox;
class QPushButton;
class QSpacerItem;
namespace Messager {
class AccountDialog final : public QDialog {
    Q_OBJECT
  public:
    // 构造账号操作窗口，初始化后续操作需要的状态或依赖。
    explicit AccountDialog(ChatService* service, QWidget* parent = nullptr);
  signals:
    // 通知窗口调用方用户提交了账号认证请求。
    void authenticated();
    // 通知窗口调用方本机登录首选项保存失败。
    void preferenceSaveFailed();

  protected:
    // 在 Qt 的绘制回调中渲染账号操作窗口的当前外观。
    void paintEvent(QPaintEvent*) override;

  private:
    // 设置账号操作窗口使用的注册。
    void setRegistration(bool registration);
    // 设置账号操作窗口使用的密码重置。
    void setPasswordReset(bool reset);
    // 显示邮箱验证码登录。
    void showEmailLogin();
    // 提交账号操作窗口。
    void submit();
    // 发送注册验证码。
    void sendRegistrationCode();
    // 更新验证码按钮。
    void updateCodeButton();
    // 显示连接设置。
    void showConnectionSettings();
    // 加载已记住的。
    void loadRemembered();
    // 应用样式。
    void applyStyle();
    // 设置账号操作窗口使用的Busy。
    void setBusy(bool busy);
    // 显示错误。
    void showError(const QString& text);
    ChatService* m_service;
    QJsonObject m_config;
    QLineEdit *m_username, *m_password, *m_confirmation;
    QLabel *m_tagline, *m_confirmationLabel, *m_error;
    QCheckBox *m_rememberUser, *m_rememberPassword;
    QPushButton *m_submit, *m_switch, *m_connection;
    QPushButton* m_forgot;
    QLineEdit *m_email, *m_emailCode;
    QPushButton* m_sendCode;
    QLabel* m_codeStatus;
    QElapsedTimer m_codeClock;
    QHash<QString, qint64> m_codeCooldowns;
    QHash<QSpacerItem*, int> m_regularGaps;
    bool m_sendingCode = false;
    bool m_registration = false, m_busy = false;
    bool m_reset = false;
    QString m_pendingPassword;
};
} // namespace Messager
