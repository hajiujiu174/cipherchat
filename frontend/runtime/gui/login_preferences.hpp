#pragma once
#include "business_worker.hpp"
#include "ccfw/local_store.hpp"
#include <QObject>
#include <QSettings>
#include <QVariantMap>
#include <memory>

namespace ccfw::gui {
class LoginPreferences final : public QObject {
    Q_OBJECT
public:
    // 构造本机记住账号设置，初始化后续操作需要的状态或依赖。
    explicit LoginPreferences(QObject* parent = nullptr) : QObject(parent) {}
    // 销毁本机记住账号设置时释放它持有的资源。
    ~LoginPreferences() override;
    // 返回本机记住账号设置使用的记住账号。
    bool rememberAccount() const { return rememberAccount_; }
    // 返回本机记住账号设置使用的记住密码。
    bool rememberPassword() const { return rememberPassword_; }
    // 报告账号首选项组件当前是否正在认证。
    bool authenticating() const { return !pendingUser_.isEmpty(); }
    // 返回本机记住账号设置的失败时的错误信息。
    QString error() const { return error_; }
    // 设置本机记住账号设置使用的上下文。
    void setContext(const QVariantMap& config);
    // 设置本机记住账号设置使用的记住账号。
    void setRememberAccount(bool selected);
    // 设置本机记住账号设置使用的记住密码。
    void setRememberPassword(bool selected);
    // 返回本机记住账号设置使用的初始用户名。
    QString initialUsername(const QString& explicitUsername) const;
    // 返回本机记住账号设置使用的密码For。
    QString passwordFor(const QString& username);
    // 开始认证。
    void beginAuthentication(const Authenticate& request);
    // 取消认证。
    void cancelAuthentication();
    // 取出并处理本机记住账号设置。
    void consume(const WorkerEvent& event);
signals:
    // 通知订阅方组件状态已经变化。
    void changed();
    // 返回本机记住账号设置使用的错误Occurred。
    void errorOccurred(const QString& message);
private:
    // 保存选中项。
    void saveSelections();
    // 生成失败结果本机记住账号设置。
    void fail(const QString& message);
    // 忘记待处理。
    void forgetPending();
    std::unique_ptr<QSettings> settings_;
    PlatformSecretProtector protector_;
    QString directory_, context_, error_, pendingUser_, pendingContext_;
    std::string pendingPassword_;
    bool rememberAccount_ = false, rememberPassword_ = false, sawAuthentication_ = false;
};
}
