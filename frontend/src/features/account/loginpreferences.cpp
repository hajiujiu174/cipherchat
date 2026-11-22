#include "loginpreferences.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on
#endif
namespace Messager {
namespace {
// 保护登录与账号。
QByteArray protect(QByteArray bytes, QByteArray entropy, bool encrypt) {
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    DATA_BLOB output{};
    bool ok = encrypt ? CryptProtectData(&input, L"NEXTALK remembered login", &extra, nullptr, nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN, &output)
                      : CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr,
                                           CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(bytes.data(), bytes.size());
    if (!ok)
        return {};
    QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
#else
    Q_UNUSED(bytes)
    Q_UNUSED(entropy)
    Q_UNUSED(encrypt)
    return {};
#endif
}
} // namespace
// 返回本机记住账号设置使用的键。
QString LoginPreferences::key(const QJsonObject& connection) {
    QJsonObject scope;
    for (const auto& name : {"host", "port", "serverName", "accountScope", "dataDirectory"})
        scope[name] = connection[name];
    QFile certificate(connection["caFile"].toString());
    if (certificate.open(QIODevice::ReadOnly))
        scope["certificate"] = QString::fromLatin1(
            QCryptographicHash::hash(certificate.readAll(), QCryptographicHash::Sha256).toHex());
    return "loginProfiles/" +
           QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(scope).toJson(QJsonDocument::Compact),
                                                        QCryptographicHash::Sha256)
                                   .toHex());
}
// 返回本机记住账号设置使用的支持密码。
bool LoginPreferences::supportsPassword() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
// 从本机设置读取并恢复本机记住账号设置。
RememberedLogin LoginPreferences::load(const QJsonObject& connection) {
    RememberedLogin login;
    QSettings settings;
    const auto group = key(connection);
    settings.beginGroup(group);
    login.rememberUsername = settings.value("rememberUsername", false).toBool();
    if (login.rememberUsername)
        login.username = settings.value("username").toString();
    if (login.rememberUsername && supportsPassword() && settings.value("rememberPassword", false).toBool()) {
        auto plain = protect(QByteArray::fromBase64(settings.value("protectedPassword").toByteArray()),
                             (group + "/" + login.username).toUtf8(), false);
        login.password = QString::fromUtf8(plain);
        plain.fill('\0');
        login.rememberPassword = !login.password.isEmpty();
    }
    return login;
}
// 将本机记住账号设置保存到本机设置，以便下次打开时恢复。
bool LoginPreferences::save(const QJsonObject& connection, const QString& username, const QString& password,
                            bool rememberUsername, bool rememberPassword) {
    QSettings settings;
    auto group = key(connection);
    settings.beginGroup(group);
    settings.remove("");
    if (!rememberUsername) {
        settings.sync();
        return settings.status() == QSettings::NoError;
    }
    settings.setValue("rememberUsername", true);
    settings.setValue("username", username);
    bool savedPassword = true;
    if (rememberPassword) {
        auto encrypted = protect(password.toUtf8(), (group + "/" + username).toUtf8(), true);
        savedPassword = !encrypted.isEmpty();
        if (savedPassword) {
            settings.setValue("rememberPassword", true);
            settings.setValue("protectedPassword", encrypted.toBase64());
        }
    }
    settings.sync();
    return savedPassword && settings.status() == QSettings::NoError;
}
// 清除本机记住账号设置。
void LoginPreferences::clear(const QJsonObject& connection) {
    QSettings settings;
    settings.remove(key(connection));
    settings.sync();
}
// 清除密码。
void LoginPreferences::clearPassword(const QJsonObject& connection) {
    QSettings settings;
    settings.beginGroup(key(connection));
    settings.remove("protectedPassword");
    settings.remove("rememberPassword");
    settings.sync();
}
} // namespace Messager
