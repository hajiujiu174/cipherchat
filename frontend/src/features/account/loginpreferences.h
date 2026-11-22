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
    // 返回本机记住账号设置使用的支持密码。
    static bool supportsPassword();
    // 加载本机记住账号设置。
    static RememberedLogin load(const QJsonObject& connection);
    // 保存本机记住账号设置。
    static bool save(const QJsonObject& connection, const QString& username, const QString& password,
                     bool rememberUsername, bool rememberPassword);
    // 清除本机记住账号设置。
    static void clear(const QJsonObject& connection);
    // 清除密码。
    static void clearPassword(const QJsonObject& connection);

  private:
    // 返回本机记住账号设置使用的键。
    static QString key(const QJsonObject& connection);
};
} // namespace Messager
