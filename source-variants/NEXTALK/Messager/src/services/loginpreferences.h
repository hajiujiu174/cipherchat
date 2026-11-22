#pragma once
#include <QJsonObject>
#include <QString>
namespace Messager {
struct RememberedLogin {
    QString username, password;
    bool rememberUsername = false, rememberPassword = false;
};
class LoginPreferences {
  public:
    static bool supportsPassword();
    static RememberedLogin load(const QJsonObject& connection);
    static bool save(const QJsonObject& connection, const QString& username, const QString& password,
                     bool rememberUsername, bool rememberPassword);
    static void clear(const QJsonObject& connection);
    static void clearPassword(const QJsonObject& connection);

  private:
    static QString key(const QJsonObject& connection);
};
} // namespace Messager
