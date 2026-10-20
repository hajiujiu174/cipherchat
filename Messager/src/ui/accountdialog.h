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
    explicit AccountDialog(ChatService* service, QWidget* parent = nullptr);
  signals:
    void authenticated();
    void preferenceSaveFailed();

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    void setRegistration(bool registration);
    void setPasswordReset(bool reset);
    void showEmailLogin();
    void submit();
    void sendRegistrationCode();
    void updateCodeButton();
    void showConnectionSettings();
    void loadRemembered();
    void applyStyle();
    void setBusy(bool busy);
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
