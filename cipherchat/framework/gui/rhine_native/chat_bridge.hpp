#pragma once
#include "business_worker.hpp"
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <functional>
#include <memory>

namespace ccfw::gui {
// Qt Quick adapter over the existing business worker. Commands and events use
// the same JSON shapes as the Rhine web bridge (rhine_json), converted to QVariant.
class ChatBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(bool hotSwitch READ hotSwitch CONSTANT)
public:
    explicit ChatBridge(QVariantMap config, QObject* parent = nullptr);
    ChatBridge(QVariantMap config, BusinessWorker& worker, QObject* parent = nullptr);
    ~ChatBridge() override;
    QVariantMap config() const { return config_; }
    bool nativeAvailable() const;
    bool hotSwitch() const { return !ownedWorker_; }
    void publish(const WorkerEvent& event);
    std::function<QString(const QString&)> frontendSwitch;
    std::function<bool(Command)> commandDispatcher;
    std::function<void()> showUpdates;
    std::function<void(const QString&, const QString&)> featureAction;
    Q_INVOKABLE void feature(const QString& action, const QString& value = {}) { if (featureAction) featureAction(action, value); }
    Q_INVOKABLE void openUpdates() { if (showUpdates) showUpdates(); }
    // Returns an empty string on success, otherwise a user-facing error.
    // Connection settings edited in 连接配置; used by the next login and by UI switching.
    Q_INVOKABLE void setConfig(const QVariantMap& values);
    Q_INVOKABLE QString command(const QVariantMap& request);
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    Q_INVOKABLE QString saveCopy(const QString& source, const QUrl& target) const;
    Q_INVOKABLE void reveal(const QString& path) const;
    Q_INVOKABLE QString switchFrontend(const QString& design);
    Q_INVOKABLE QVariant setting(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
signals:
    void events(const QVariantList& events);
    void configChanged();
private:
    void drain();
    std::unique_ptr<BusinessWorker> ownedWorker_;
    BusinessWorker* worker_;
    QVariantMap config_;
    QTimer pump_;
    QSettings settings_;
};
}
