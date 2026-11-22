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
    explicit LoginPreferences(QObject* parent = nullptr) : QObject(parent) {}
    ~LoginPreferences() override;
    bool rememberAccount() const { return rememberAccount_; }
    bool rememberPassword() const { return rememberPassword_; }
    bool authenticating() const { return !pendingUser_.isEmpty(); }
    QString error() const { return error_; }
    void setContext(const QVariantMap& config);
    void setRememberAccount(bool selected);
    void setRememberPassword(bool selected);
    QString initialUsername(const QString& explicitUsername) const;
    QString passwordFor(const QString& username);
    void beginAuthentication(const Authenticate& request);
    void cancelAuthentication();
    void consume(const WorkerEvent& event);
signals:
    void changed();
    void errorOccurred(const QString& message);
private:
    void saveSelections();
    void fail(const QString& message);
    void forgetPending();
    std::unique_ptr<QSettings> settings_;
    PlatformSecretProtector protector_;
    QString directory_, context_, error_, pendingUser_, pendingContext_;
    std::string pendingPassword_;
    bool rememberAccount_ = false, rememberPassword_ = false, sawAuthentication_ = false;
};
}
