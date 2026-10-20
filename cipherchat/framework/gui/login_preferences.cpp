#include "login_preferences.hpp"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <openssl/crypto.h>
#include "common/crypto/crypto.hpp"

namespace ccfw::gui {
namespace {
QVariantMap configuration(const Authenticate& request) {
    return {{"dataDirectory",QString::fromStdWString(request.dataDirectory.wstring())},
            {"host",QString::fromStdString(request.connection.host)}, {"port",request.connection.port},
            {"serverName",QString::fromStdString(request.connection.expectedServerName)},
            {"caFile",QString::fromStdWString(request.connection.caFile.wstring())}};
}
QString serverContext(const QVariantMap& config) {
    QFile certificate(config.value("caFile").toString());
    QString trust;
    if (certificate.open(QIODevice::ReadOnly)) {
        const auto bytes = certificate.readAll();
        trust = QString::fromLatin1(QByteArray::fromStdString(cc::crypto::sha256(cc::BytesView(bytes.constData(),static_cast<std::size_t>(bytes.size())))).toHex());
    } else trust = "unreadable:" + QFileInfo(config.value("caFile").toString()).absoluteFilePath();
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"host",config.value("host").toString().trimmed().toLower()}, {"port",config.value("port").toInt()},
        {"serverName",config.value("serverName").toString()},
        {"trust",trust}}).toJson(QJsonDocument::Compact));
}
}
LoginPreferences::~LoginPreferences() { forgetPending(); }
void LoginPreferences::forgetPending() {
    OPENSSL_cleanse(pendingPassword_.data(),pendingPassword_.size()); pendingPassword_.clear();
    pendingUser_.clear(); pendingContext_.clear(); sawAuthentication_ = false;
}
void LoginPreferences::cancelAuthentication() { forgetPending(); emit changed(); }
void LoginPreferences::fail(const QString& message) { error_ = message; emit errorOccurred(message); }
void LoginPreferences::setContext(const QVariantMap& config) {
    const auto directory = QDir(config.value("dataDirectory").toString()).absolutePath();
    const auto context = serverContext(config);
    if (settings_ && directory_ == directory && context_ == context) return;
    forgetPending(); directory_ = directory; context_ = context; error_.clear();
    settings_ = std::make_unique<QSettings>(QDir(directory).filePath("preferences.ini"),QSettings::IniFormat);
    rememberAccount_ = settings_->value("login/rememberAccount",false).toBool();
    rememberPassword_ = rememberAccount_ && settings_->value("login/rememberPassword",false).toBool();
    emit changed();
}
void LoginPreferences::saveSelections() {
    if (!settings_) return;
    settings_->setValue("login/rememberAccount",rememberAccount_);
    settings_->setValue("login/rememberPassword",rememberPassword_);
    if (!rememberAccount_) { settings_->remove("login/username"); settings_->remove("login/context"); }
    if (!rememberPassword_) settings_->remove("login/protectedPassword");
    settings_->sync();
    if (settings_->status() != QSettings::NoError) fail(QStringLiteral("登录选项保存失败，请检查本地数据目录。"));
}
void LoginPreferences::setRememberAccount(bool selected) {
    error_.clear(); rememberAccount_ = selected;
    if (!selected) { rememberPassword_ = false; OPENSSL_cleanse(pendingPassword_.data(),pendingPassword_.size()); pendingPassword_.clear(); }
    saveSelections(); emit changed();
}
void LoginPreferences::setRememberPassword(bool selected) {
    error_.clear(); rememberPassword_ = selected;
    if (selected) rememberAccount_ = true;
    else { OPENSSL_cleanse(pendingPassword_.data(),pendingPassword_.size()); pendingPassword_.clear(); }
    saveSelections(); emit changed();
}
QString LoginPreferences::initialUsername(const QString& explicitUsername) const {
    if (!explicitUsername.isEmpty()) return explicitUsername;
    if (!settings_ || !rememberAccount_ || settings_->value("login/context").toString() != context_) return {};
    return settings_->value("login/username").toString();
}
QString LoginPreferences::passwordFor(const QString& username) {
    if (!settings_ || !rememberPassword_ || username.isEmpty() || settings_->value("login/username").toString() != username
        || settings_->value("login/context").toString() != context_) return {};
    const auto blob = settings_->value("login/protectedPassword").toByteArray();
    if (blob.isEmpty()) return {};
    auto plain = protector_.unseal(cc::BytesView(blob.constData(),static_cast<std::size_t>(blob.size())));
    if (!plain) { fail(QStringLiteral("已保存的密码无法读取，请重新输入。")); return {}; }
    auto& bytes = plain.value();
    const auto document = QJsonDocument::fromJson(QByteArray::fromRawData(bytes.data(),static_cast<int>(bytes.size()))).object();
    const bool matches = document.value("purpose").toString() == "CipherChat/login/v1"
        && document.value("username").toString() == username && document.value("context").toString() == context_;
    const auto password = matches ? document.value("password").toString() : QString{};
    OPENSSL_cleanse(bytes.data(),bytes.size());
    if (!matches) fail(QStringLiteral("已保存的密码与当前账号或服务器不匹配，请重新输入。"));
    return password;
}
void LoginPreferences::beginAuthentication(const Authenticate& request) {
    setContext(configuration(request)); forgetPending(); error_.clear();
    pendingUser_ = QString::fromStdString(request.username); pendingContext_ = context_;
    if (rememberPassword_) pendingPassword_ = request.password;
    emit changed();
}
void LoginPreferences::consume(const WorkerEvent& event) {
    if (!authenticating()) return;
    if (const auto* client = std::get_if<ClientEvent>(&event)) {
        if (const auto* account = std::get_if<AccountEvent>(client)) {
            if (account->phase == AccountPhase::Registering || account->phase == AccountPhase::ChallengePending) sawAuthentication_ = true;
            else if (account->phase == AccountPhase::Disconnected && sawAuthentication_) cancelAuthentication();
        }
        if (std::holds_alternative<Error>(*client)) cancelAuthentication();
        return;
    }
    const auto* restored = std::get_if<RestoredView>(&event);
    if (!restored || !sawAuthentication_ || QString::fromStdString(restored->username) != pendingUser_ || pendingContext_ != context_) return;
    if (rememberAccount_) {
        settings_->setValue("login/username",pendingUser_); settings_->setValue("login/context",context_);
        settings_->remove("login/protectedPassword");
        if (rememberPassword_ && !pendingPassword_.empty()) {
            auto plain = QJsonDocument(QJsonObject{{"purpose","CipherChat/login/v1"},{"username",pendingUser_},
                {"context",context_},{"password",QString::fromStdString(pendingPassword_)}}).toJson(QJsonDocument::Compact);
            auto sealed = protector_.seal(cc::BytesView(plain.constData(),static_cast<std::size_t>(plain.size())));
            OPENSSL_cleanse(plain.data(),static_cast<std::size_t>(plain.size()));
            if (sealed) settings_->setValue("login/protectedPassword",QByteArray::fromStdString(sealed.value()));
            else fail(QStringLiteral("登录成功，但密码保存失败；下次请重新输入。"));
        }
        saveSelections();
    }
    forgetPending(); emit changed();
}
}
