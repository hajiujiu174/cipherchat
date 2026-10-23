#include "accountdialog.h"
#include "components.h"
#include "services/loginpreferences.h"
#include "theme.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QDateTime>
#include <memory>
namespace Messager {
namespace {
class RememberCheck final : public QCheckBox {
  public:
    using QCheckBox::QCheckBox;

  protected:
    void paintEvent(QPaintEvent* event) override {
        QCheckBox::paintEvent(event);
        if (!isChecked())
            return;
        QStyleOptionButton option;
        initStyleOption(&option);
        auto r = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath check;
        check.moveTo(r.left() + 7, r.top() + 13);
        check.lineTo(r.left() + 11, r.top() + 17);
        check.lineTo(r.left() + 20, r.top() + 8);
        p.drawPath(check);
    }
};
class BrandMark final : public QLabel {
  public:
    BrandMark() { setFixedSize(78,78);setPixmap(applicationIcon().pixmap(78,78));setAccessibleName("NEXTALK 聊天图标"); }
};
} // namespace
AccountDialog::AccountDialog(ChatService* service, QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                          Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint),
      m_service(service), m_config(service->configuration()) {
    setObjectName("accountDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::NonModal);
    setFixedWidth(530);
    setWindowIcon(applicationIcon());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(35, 28, 35, 24);
    layout->setSpacing(0);
    layout->addWidget(new BrandMark, 0, Qt::AlignHCenter);
    layout->addSpacing(15);
    auto* title = new QLabel("NEXTALK");
    title->setObjectName("authTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    layout->addSpacing(12);
    m_tagline = new QLabel;
    m_tagline->setObjectName("authTagline");
    m_tagline->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_tagline);
    layout->addSpacing(31);
    auto addField = [layout](const QString& title, const QString& objectName, QLineEdit** input) {
        auto* caption = new QLabel(title);
        caption->setProperty("authRole", "caption");
        layout->addWidget(caption);
        layout->addSpacing(12);
        *input = new QLineEdit;
        (*input)->setObjectName(objectName);
        (*input)->setAccessibleName(title);
        (*input)->setFixedHeight(54);
        layout->addWidget(*input);
        return caption;
    };
    addField("用户名或邮箱", "accountUsername", &m_username)->setObjectName("accountUsernameCaption");
    m_username->setPlaceholderText("请输入用户名或已绑定邮箱");
    layout->addSpacing(17);
    addField("密码", "accountPassword", &m_password);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText("请输入密码");
    auto* confirmationArea = new QWidget;
    confirmationArea->setObjectName("confirmationArea");
    auto* confirmationLayout = new QVBoxLayout(confirmationArea);
    confirmationLayout->setContentsMargins(0, 17, 0, 0);
    confirmationLayout->setSpacing(12);
    m_confirmationLabel = new QLabel("确认密码");
    m_confirmationLabel->setProperty("authRole", "caption");
    m_confirmation = new QLineEdit;
    m_confirmation->setObjectName("accountConfirmation");
    m_confirmation->setAccessibleName("确认密码");
    m_confirmation->setEchoMode(QLineEdit::Password);
    m_confirmation->setPlaceholderText("请再次输入密码");
    m_confirmation->setFixedHeight(54);
    confirmationLayout->addWidget(m_confirmationLabel);
    confirmationLayout->addWidget(m_confirmation);
    layout->addWidget(confirmationArea);
    auto* emailArea = new QWidget;
    emailArea->setObjectName("registrationEmailArea");
    auto* emailLayout = new QVBoxLayout(emailArea);
    emailLayout->setContentsMargins(0, 17, 0, 0);
    emailLayout->setSpacing(10);
    auto* emailCaption = new QLabel("注册邮箱"); emailCaption->setProperty("authRole", "caption"); emailCaption->setObjectName("accountEmailCaption");
    emailLayout->addWidget(emailCaption);
    m_email = new QLineEdit;
    m_email->setObjectName("registrationEmail"); m_email->setAccessibleName("注册邮箱");
    m_email->setPlaceholderText("请输入可接收验证码的邮箱"); m_email->setMaxLength(254);
    m_email->setFixedHeight(44); m_email->setInputMethodHints(Qt::ImhEmailCharactersOnly);
    emailLayout->addWidget(m_email);
    auto* codeCaption = new QLabel("邮箱验证码"); codeCaption->setProperty("authRole", "caption");
    emailLayout->addWidget(codeCaption);
    auto* codeRow = new QHBoxLayout;
    m_emailCode = new QLineEdit;
    m_emailCode->setObjectName("registrationEmailCode"); m_emailCode->setAccessibleName("邮箱验证码");
    m_emailCode->setPlaceholderText("6 位验证码"); m_emailCode->setMaxLength(6); m_emailCode->setFixedHeight(44);
    m_emailCode->setInputMethodHints(Qt::ImhDigitsOnly);
    m_sendCode = new QPushButton("发送验证码"); m_sendCode->setObjectName("sendRegistrationCode");
    m_sendCode->setAutoDefault(false); m_sendCode->setFixedSize(145,44);
    codeRow->addWidget(m_emailCode,1); codeRow->addWidget(m_sendCode);
    emailLayout->addLayout(codeRow);
    m_codeStatus = new QLabel; m_codeStatus->setObjectName("registrationCodeStatus");
    m_codeStatus->setWordWrap(true); m_codeStatus->hide(); emailLayout->addWidget(m_codeStatus);
    layout->addWidget(emailArea);
    m_codeClock.start();
    auto* codeTimer = new QTimer(this); codeTimer->setInterval(250);
    connect(codeTimer, &QTimer::timeout, this, &AccountDialog::updateCodeButton); codeTimer->start();
    connect(m_sendCode, &QPushButton::clicked, this, &AccountDialog::sendRegistrationCode);
    connect(m_email, &QLineEdit::textChanged, this, [this] { m_emailCode->clear(); m_codeStatus->hide(); updateCodeButton(); });
    connect(m_username, &QLineEdit::textChanged, this, [this] { m_emailCode->clear(); m_codeStatus->hide(); });
    connect(service, &ChatService::registrationCodeResult, this, [this](bool ok, const QString& email, const QString& error, int retryAfter) {
        if (!m_sendingCode) return;
        m_sendingCode = false;
        if (retryAfter > 0) m_codeCooldowns[(m_reset ? "reset:" : "register:") + email.trimmed().toLower()] = m_codeClock.elapsed() + qBound(1,retryAfter,3600)*1000;
        setBusy(false);
        if (ok) { m_codeStatus->setText("验证码已提交发送，请查收邮箱（10 分钟内有效）"); m_codeStatus->show(); m_emailCode->setFocus(); adjustSize(); }
        else showError(error.isEmpty() ? "验证码发送失败，请稍后重试" : error);
    });
    layout->addSpacing(18);
    auto* remember = new QHBoxLayout;
    remember->setSpacing(29);
    m_rememberUser = new RememberCheck("记住账号");
    m_rememberUser->setObjectName("rememberUsername");
    m_rememberPassword = new RememberCheck("记住密码");
    m_rememberPassword->setObjectName("rememberPassword");
    m_rememberPassword->setEnabled(LoginPreferences::supportsPassword());
    m_rememberPassword->setToolTip(LoginPreferences::supportsPassword()
                                       ? "由当前 Windows 用户的系统加密保护保存"
                                       : "本平台尚未配置安全凭据存储");
    remember->addWidget(m_rememberUser);
    remember->addWidget(m_rememberPassword);
    remember->addStretch();
    layout->addLayout(remember);
    layout->addSpacing(23);
    m_submit = new QPushButton;
    m_submit->setObjectName("accountSubmit");
    m_submit->setFixedHeight(56);
    m_submit->setDefault(true);
    layout->addWidget(m_submit);
    layout->addSpacing(20);
    m_error = new QLabel;
    m_error->setObjectName("accountError");
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);
    auto* footer = new QHBoxLayout;
    m_switch = new QPushButton;
    m_switch->setObjectName("registerTab");
    m_switch->setProperty("authRole", "link");
    m_switch->setAutoDefault(false);
    m_connection = new QPushButton("连接设置");
    m_connection->setObjectName("connectionSettingsButton");
    m_connection->setProperty("authRole", "secondaryLink");
    m_connection->setAutoDefault(false);
    m_forgot = new QPushButton("忘记密码");
    m_forgot->setObjectName("forgotPassword"); m_forgot->setProperty("authRole","link"); m_forgot->setAutoDefault(false);
    footer->addWidget(m_switch);
    footer->addWidget(m_forgot);
    auto* emailLogin=new QPushButton("邮箱验证码登录");emailLogin->setObjectName("emailLoginEntry");emailLogin->setProperty("authRole","link");emailLogin->setAutoDefault(false);
    footer->addWidget(emailLogin);
    connect(emailLogin,&QPushButton::clicked,this,&AccountDialog::showEmailLogin);
    footer->addStretch();
    footer->addWidget(m_connection);
    layout->addLayout(footer);
    auto* updates=new QHBoxLayout;
    auto* updateStatus=new QLabel("启动后自动检查新版本");updateStatus->setObjectName("loginUpdateStatus");updateStatus->setTextFormat(Qt::PlainText);updateStatus->setWordWrap(true);updateStatus->setProperty("authRole","caption");updates->addWidget(updateStatus,1);
    auto* updateEntry=new QPushButton("软件更新");updateEntry->setObjectName("loginUpdateButton");updateEntry->setProperty("authRole","secondaryLink");updateEntry->setAutoDefault(false);updates->addWidget(updateEntry);layout->addSpacing(14);layout->addLayout(updates);
    connect(m_switch, &QPushButton::clicked, this, [this] { if (m_reset) setPasswordReset(false); else setRegistration(!m_registration); });
    connect(m_forgot, &QPushButton::clicked, this, [this] { setPasswordReset(true); });
    connect(service, &ChatService::passwordResetResult, this, [this](bool ok,const QString& error) {
        if (!m_reset || !m_busy || m_sendingCode) return;
        setBusy(false); m_pendingPassword.clear(); m_password->clear(); m_confirmation->clear(); m_emailCode->clear();
        if (!ok) { showError(error.isEmpty() ? "密码重置未成功，请重新获取验证码后重试" : error); return; }
        LoginPreferences::clearPassword(m_config);
        setPasswordReset(false);
        showError("密码已重置，请使用新密码登录。本机身份和聊天记录保持不变。");
        m_password->setFocus();
    });
    connect(m_connection, &QPushButton::clicked, this, &AccountDialog::showConnectionSettings);
    connect(m_submit, &QPushButton::clicked, this, &AccountDialog::submit);
    connect(m_rememberPassword, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked)
            m_rememberUser->setChecked(true);
        else
            LoginPreferences::clearPassword(m_config);
    });
    connect(m_rememberUser, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            m_rememberPassword->setChecked(false);
            LoginPreferences::clear(m_config);
        }
    });
    connect(m_username, &QLineEdit::textEdited, this, [this] { m_password->clear(); });
    connect(service, &ChatService::operationError, this, [this](const QString& error) {
        if (!m_busy)
            return;
        m_pendingPassword.clear();
        m_sendingCode = false;
        setBusy(false);
        showError(error);
        m_password->setFocus();
    });
    connect(service, &ChatService::sessionChanged, this, [this] {
        if (!m_busy || m_sendingCode)
            return;
        if (m_service->online()) {
            const bool saved =
                LoginPreferences::save(m_config, m_username->text().trimmed(), m_pendingPassword,
                                       m_rememberUser->isChecked(), m_rememberPassword->isChecked());
            m_pendingPassword.clear();
            m_password->clear();
            m_confirmation->clear();
            m_busy = false;
            emit authenticated();
            if (!saved)
                emit preferenceSaveFailed();
            accept();
        } else {
            m_submit->setText(m_service->statusText());
        }
    });
    connect(&Theme::instance(), &Theme::changed, this, &AccountDialog::applyStyle);
    setRegistration(false);
    loadRemembered();
    applyStyle();
    resize(530, sizeHint().height());
}
void AccountDialog::setRegistration(bool registration) {
    m_reset = false;
    m_rememberUser->show(); m_rememberPassword->show();
    findChild<QLabel*>("accountEmailCaption")->setText("注册邮箱");
    m_registration = registration;
    findChild<QPushButton*>("emailLoginEntry")->setVisible(!registration);
    findChild<QLabel*>("accountUsernameCaption")->setText(registration ? "用户名" : "用户名或邮箱");
    m_username->setAccessibleName(registration ? "用户名" : "用户名或邮箱");
    m_username->setPlaceholderText(registration ? "请输入用户名" : "请输入用户名或已绑定邮箱");
    m_username->setMaxLength(registration ? 32 : 254);
    m_forgot->setVisible(!registration);
    m_password->setPlaceholderText("请输入密码");
    m_confirmationLabel->setText("确认密码");
    m_email->setAccessibleName("注册邮箱"); m_email->setPlaceholderText("请输入可接收验证码的邮箱");
    auto* outer = qobject_cast<QVBoxLayout*>(layout());
    outer->setContentsMargins(35, registration ? 16 : 28, 35, registration ? 16 : 24);
    for (int i = 0; i < outer->count(); ++i) {
        if (auto* gap = outer->itemAt(i)->spacerItem()) {
            if (!m_regularGaps.contains(gap)) m_regularGaps[gap] = gap->sizeHint().height();
            gap->changeSize(0, registration ? qMin(m_regularGaps[gap], 12) : m_regularGaps[gap], QSizePolicy::Minimum, QSizePolicy::Fixed);
        }
    }
    setWindowTitle(registration ? "NEXTALK · 注册" : "NEXTALK · 登录");
    m_tagline->setText(registration ? "创建账号 · 开始聊天" : "与联系人保持联系");
    findChild<QWidget*>("confirmationArea")->setVisible(registration);
    findChild<QWidget*>("registrationEmailArea")->setVisible(registration);
    findChild<QWidget*>("confirmationArea")->layout()->setContentsMargins(0, registration ? 10 : 17, 0, 0);
    findChild<QWidget*>("registrationEmailArea")->layout()->setContentsMargins(0, 10, 0, 0);
    // Keep the register form within an ordinary desktop height.
    for (auto* mark : findChildren<QWidget*>()) if (mark->accessibleName() == "NEXTALK 锁形标志") mark->setVisible(!registration);
    for (auto* field : {m_username, m_password, m_confirmation}) field->setFixedHeight(registration ? 44 : 54);
    m_emailCode->clear(); m_codeStatus->hide();
    updateCodeButton();
    m_submit->setText(registration ? "注册并登录" : "登录");
    m_switch->setText(registration ? "返回登录" : "注册账号");
    m_password->clear();
    m_confirmation->clear();
    m_error->hide();
    QTimer::singleShot(0, this, [this] { adjustSize(); });
}
void AccountDialog::setPasswordReset(bool reset) {
    setRegistration(reset); m_reset=reset;
    if (!reset) return;
    m_registration=false;
    m_rememberUser->hide(); m_rememberPassword->hide();
    findChild<QLabel*>("accountEmailCaption")->setText("已绑定邮箱");
    setWindowTitle("NEXTALK · 重置密码");
    m_tagline->setText("使用已验证邮箱重置密码");
    m_password->setPlaceholderText("请输入至少 6 位的新密码");
    m_confirmationLabel->setText("确认新密码");
    m_email->setAccessibleName("已绑定邮箱"); m_email->setPlaceholderText("请输入注册时已验证的邮箱");
    m_submit->setText("重置密码"); m_switch->setText("返回登录"); m_forgot->hide();
}
void AccountDialog::loadRemembered() {
    const auto saved = LoginPreferences::load(m_config);
    QSignalBlocker userBlock(m_rememberUser), passwordBlock(m_rememberPassword);
    const auto explicitUser = m_config.value("username").toString();
    m_username->setText(explicitUser.isEmpty() ? saved.username : explicitUser);
    m_password->setText(explicitUser.isEmpty() || explicitUser == saved.username ? saved.password
                                                                                 : QString());
    m_rememberUser->setChecked(saved.rememberUsername);
    m_rememberPassword->setChecked(saved.rememberPassword);
}
void AccountDialog::showError(const QString& text) {
    m_error->setText(text);
    m_error->show();
    adjustSize();
}
void AccountDialog::setBusy(bool busy) {
    m_busy = busy;
    for (QWidget* widget : QList<QWidget*>{m_username, m_password, m_confirmation, m_rememberUser,
                                           m_rememberPassword, m_submit, m_switch, m_connection, m_email, m_emailCode, m_forgot})
        widget->setEnabled(!busy);
    if (!busy) {
        m_rememberPassword->setEnabled(LoginPreferences::supportsPassword());
        m_submit->setText(m_reset ? "重置密码" : m_registration ? "注册并登录" : "登录");
    }
    updateCodeButton();
}
void AccountDialog::updateCodeButton() {
    const auto remaining = qMax(qint64(0), m_codeCooldowns.value((m_reset ? "reset:" : "register:") + m_email->text().trimmed().toLower()) - m_codeClock.elapsed());
    m_sendCode->setEnabled(!m_busy && remaining == 0);
    m_sendCode->setText(m_sendingCode ? "正在发送…" : remaining > 0 ? QString("%1 秒后重发").arg((remaining+999)/1000) : "发送验证码");
}
void AccountDialog::sendRegistrationCode() {
    if (m_busy) return;
    if (m_username->text().trimmed().isEmpty()) { showError(m_reset ? "请先填写需要重置密码的用户名" : "请先填写用户名，验证码将用于该账号注册"); return; }
    const auto email = m_email->text().trimmed();
    if (!QRegularExpression(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)").match(email).hasMatch()) { showError("请输入有效的邮箱地址"); m_email->setFocus(); return; }
    m_error->hide(); m_codeStatus->hide(); m_emailCode->clear();
    auto request = m_config;
    request["username"] = m_username->text().trimmed(); request["email"] = email;
    request["passwordReset"] = m_reset;
    m_sendingCode = true; setBusy(true);
    m_service->requestRegistrationCode(request);
}
void AccountDialog::submit() {
    if (m_busy)
        return;
    if (m_username->text().trimmed().isEmpty() || m_password->text().isEmpty()) {
        showError(m_registration || m_reset ? "请输入用户名和密码" : "请输入用户名或邮箱，以及密码");
        return;
    }
    if ((m_registration || m_reset) && m_password->text() != m_confirmation->text()) {
        showError("两次输入的密码不一致");
        m_confirmation->setFocus();
        return;
    }
    if (m_registration || m_reset) {
        if (!QRegularExpression(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)").match(m_email->text().trimmed()).hasMatch()) { showError(m_reset ? "请输入已绑定的邮箱" : "请输入有效的注册邮箱"); m_email->setFocus(); return; }
        if (!QRegularExpression("^[0-9]{6}$").match(m_emailCode->text()).hasMatch()) { showError("请输入 6 位邮箱验证码"); m_emailCode->setFocus(); return; }
    }
    m_error->hide();
    auto request = m_config;
    request["username"] = m_username->text().trimmed();
    request["password"] = m_password->text();
    request["registerNew"] = m_registration;
    if (m_registration || m_reset) { request["email"] = m_email->text().trimmed(); request["emailCode"] = m_emailCode->text(); }
    else { request.remove("email"); request.remove("emailCode"); }
    m_emailCode->clear();
    if (request.value("accountScope").isNull())
        request.remove("accountScope");
    request["directEnabled"] = m_config.value("directEnabled").toBool(false);
    m_pendingPassword = m_password->text();
    m_password->clear();
    m_confirmation->clear();
    setBusy(true);
    m_submit->setText(m_reset ? "正在重置…" : m_registration ? "正在注册…" : "正在登录…");
    if (m_reset) m_service->resetPassword(request); else m_service->authenticate(request);
}
void AccountDialog::showConnectionSettings() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("connectionDialog");
    dialog->setWindowTitle("NEXTALK · 连接设置");
    dialog->setMinimumWidth(500);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    auto* form = new QFormLayout;
    QList<QPair<QString, QLineEdit*>> inputs;
    const QList<QPair<QString, QString>> fields{{"host", "服务器"},
                                                {"port", "端口"},
                                                {"caFile", "服务器证书"},
                                                {"serverName", "证书主机名"},
                                                {"dataDirectory", "账号数据目录"},
                                                {"accountScope", "兼容账号作用域"}};
    for (const auto& field : fields) {
        auto* edit =
            new QLineEdit(field.first == "port" ? QString::number(m_config.value("port").toInt(55220))
                                                : m_config.value(field.first).toString());
        edit->setObjectName("connection_" + field.first);
        form->addRow(field.second, edit);
        inputs.append({field.first, edit});
    }
    layout->addLayout(form);
    auto* error = new QLabel;
    error->setWordWrap(true);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, inputs, error] {
        auto config = m_config;
        for (const auto& input : inputs)
            config[input.first] = input.second->text().trimmed();
        bool valid = false;
        int port = config["port"].toString().toInt(&valid);
        if (!valid || port < 1 || port > 65535 || config["host"].toString().isEmpty()) {
            error->setText("请填写服务器，并将端口设为 1–65535");
            return;
        }
        config["port"] = port;
        m_config = config;
        m_emailCode->clear(); m_codeStatus->hide();
        m_confirmation->clear();
        loadRemembered();
        dialog->accept();
    });
    dialog->open();
}
void AccountDialog::applyStyle() {
    const bool dark = Theme::instance().dark();
    const auto& c=Theme::instance().colors();const bool pencil=Theme::instance().pencil();
    const QString surface = pencil?c.surface.name():(dark ? "#1b1e28" : "#ffffff");
    const QString text = pencil?c.text.name():(dark ? "#edf0f7" : "#292638");
    const QString muted = pencil?c.muted.name():(dark ? "#a5adc2" : "#77738c");
    const QString border = pencil?c.border.name():(dark ? "#404454" : "#deddea");
    auto style=QString(R"(
      QDialog#accountDialog { background:%1; }
      QDialog#accountDialog QLabel { color:%2; background:transparent; }
      QDialog#accountDialog QLabel#authTitle { font-family:'Segoe UI'; font-size:38px; font-weight:700; }
      QDialog#accountDialog QLabel#authTagline { font-size:16px; color:%3; }
      QDialog#accountDialog QLabel[authRole="caption"] { color:%3; font-size:14px; font-weight:600; }
      QDialog#accountDialog QLineEdit { background:%1; color:%2; border:1px solid %4; border-radius:9px; padding:0 19px; font-size:19px; selection-background-color:#6653c9; }
      QDialog#accountDialog QLineEdit:focus { border:1px solid #8c72ff; }
      QDialog#accountDialog QLineEdit:disabled { color:%3; }
      QDialog#accountDialog QCheckBox { color:%2; font-size:17px; spacing:11px; }
      QDialog#accountDialog QCheckBox::indicator { width:26px; height:26px; border:1px solid %4; border-radius:7px; background:%1; }
      QDialog#accountDialog QCheckBox::indicator:checked { background:#6250c8; border-color:#6250c8; }
      QDialog#accountDialog QCheckBox::indicator:focus { border-color:#8c72ff; }
      QDialog#accountDialog QLabel#registrationCodeStatus { color:%3; font-size:12px; }
      QDialog#accountDialog QPushButton#sendRegistrationCode { color:#6250c8; background:%1; border:1px solid %4; border-radius:9px; font-size:14px; }
      QDialog#accountDialog QPushButton#sendRegistrationCode:disabled { color:%3; }
      QDialog#accountDialog QPushButton#accountSubmit { background:#6250c8; border:1px solid #5946be; color:white; border-radius:9px; font-size:20px; font-weight:600; padding:0; }
      QDialog#accountDialog QPushButton#accountSubmit:hover { background:#5743bb; }
      QDialog#accountDialog QPushButton#accountSubmit:disabled { background:#9e92d8; border-color:#9e92d8; }
      QDialog#accountDialog QPushButton[authRole="link"] { color:%3; font-size:18px; font-weight:600; border:0; background:transparent; padding:8px 22px; }
      QDialog#accountDialog QPushButton[authRole="secondaryLink"] { color:%3; font-size:12px; border:0; background:transparent; padding:8px 0; }
      QDialog#accountDialog QPushButton[authRole="link"]:hover, QDialog#accountDialog QPushButton[authRole="secondaryLink"]:hover { color:#8269e4; }
      QDialog#accountDialog QLabel#accountError { color:#cd4d66; font-size:13px; padding-bottom:10px; }
    )")
                      .arg(surface, text, muted, border);
    if(pencil)style+=QString(R"(
      QDialog#accountDialog { background:%1; }
      QDialog#accountDialog QLabel#authTitle { font-family:'KaiTi'; }
      QDialog#accountDialog QPushButton#accountSubmit, QDialog#accountDialog QPushButton#accountSubmit:hover { background:%2; color:%3; border:1px solid %4; }
      QDialog#accountDialog QPushButton#accountSubmit:disabled { background:%1; color:%5; }
      QDialog#accountDialog QPushButton#sendRegistrationCode { color:%6; }
      QDialog#accountDialog QLineEdit:focus { border-color:%6; }
      QDialog#accountDialog QCheckBox::indicator:checked { background:%6; border-color:%6; }
    )").arg(c.window.name(),c.accentSoft.name(),c.text.name(),c.border.name(),c.muted.name(),c.accent.name());
    setStyleSheet(style);
}
void AccountDialog::paintEvent(QPaintEvent* event) {
    QDialog::paintEvent(event);
    if(Theme::instance().pencil()) {QPainter p(this);paintPencilPaper(p,rect(),Theme::instance().colors().window,true);}
}
} // namespace Messager
