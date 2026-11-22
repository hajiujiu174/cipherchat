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
    // 构造Rhine 聊天界面桥接器，初始化后续操作需要的状态或依赖。
    explicit ChatBridge(QVariantMap config, QObject* parent = nullptr);
    // 构造Rhine 聊天界面桥接器，初始化后续操作需要的状态或依赖。
    ChatBridge(QVariantMap config, BusinessWorker& worker, QObject* parent = nullptr);
    // 销毁Rhine 聊天界面桥接器时释放它持有的资源。
    ~ChatBridge() override;
    // 返回Rhine 聊天界面桥接器使用的配置。
    QVariantMap config() const { return config_; }
    // 返回Rhine 聊天界面桥接器使用的原生Available。
    bool nativeAvailable() const;
    // 通知调用方当前界面需要切换到另一种前端。
    bool hotSwitch() const { return !ownedWorker_; }
    // 发布Rhine 聊天界面桥接器。
    void publish(const WorkerEvent& event);
    std::function<QString(const QString&)> frontendSwitch;
    std::function<bool(Command)> commandDispatcher;
    std::function<void()> showUpdates;
    std::function<void(const QString&, const QString&)> featureAction;
    // 更新Rhine 聊天界面桥接器中的功能状态。
    Q_INVOKABLE void feature(const QString& action, const QString& value = {}) { if (featureAction) featureAction(action, value); }
    // 打开Updates。
    Q_INVOKABLE void openUpdates() { if (showUpdates) showUpdates(); }
    // Returns an empty string on success, otherwise a user-facing error.
    // Connection settings edited in 连接配置; used by the next login and by UI switching.
    Q_INVOKABLE void setConfig(const QVariantMap& values);
    // 把 Rhine 提交的 JSON 命令转为业务线程请求，返回参数或提交错误。
    Q_INVOKABLE QString command(const QVariantMap& request);
    // 返回Rhine 聊天界面桥接器使用的本机文件路径。
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    // 保存Copy。
    Q_INVOKABLE QString saveCopy(const QString& source, const QUrl& target) const;
    // 显示位置Rhine 聊天界面桥接器。
    Q_INVOKABLE void reveal(const QString& path) const;
    // 切换前端。
    Q_INVOKABLE QString switchFrontend(const QString& design);
    // 返回Rhine 聊天界面桥接器使用的设置项。
    Q_INVOKABLE QVariant setting(const QString& key, const QVariant& fallback = {}) const;
    // 设置Rhine 聊天界面桥接器使用的设置项。
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
signals:
    // 返回Rhine 聊天界面桥接器使用的事件。
    void events(const QVariantList& events);
    // 通知监听方配置发生变化，供界面或调用方刷新。
    void configChanged();
private:
    // 读出工作线程产生的客户端事件，转换为 Rhine 可用的数据。
    void drain();
    std::unique_ptr<BusinessWorker> ownedWorker_;
    BusinessWorker* worker_;
    QVariantMap config_;
    QTimer pump_;
    QSettings settings_;
};
}
