#include "chat_bridge.hpp"
#include "../rhine_json.hpp"
#include <openssl/crypto.h>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace ccfw::gui {
namespace {
// 返回Rhine 界面使用的desktop客户端。
QString desktopClient() { return QDir(QCoreApplication::applicationDirPath()).filePath("nextalk.exe"); }
}
// 构造Rhine 聊天界面桥接器，初始化后续操作需要的状态或依赖。
ChatBridge::ChatBridge(QVariantMap config, QObject* parent)
    : QObject(parent), ownedWorker_(std::make_unique<BusinessWorker>()), worker_(ownedWorker_.get()), config_(std::move(config)),
      settings_(QDir(config_.value("dataDirectory").toString()).filePath("rhine-native.ini"), QSettings::IniFormat) {
    pump_.setInterval(60);
    connect(&pump_, &QTimer::timeout, this, &ChatBridge::drain);
    pump_.start();
}
// 构造Rhine 聊天界面桥接器，初始化后续操作需要的状态或依赖。
ChatBridge::ChatBridge(QVariantMap config, BusinessWorker& worker, QObject* parent)
    : QObject(parent), worker_(&worker), config_(std::move(config)),
      settings_(QDir(config_.value("dataDirectory").toString()).filePath("rhine-native.ini"), QSettings::IniFormat) {}
// 销毁Rhine 聊天界面桥接器时释放它持有的资源。
ChatBridge::~ChatBridge() { if (ownedWorker_) { worker_->requestStop(); worker_->join(); } }
// 返回Rhine 聊天界面桥接器使用的原生Available。
bool ChatBridge::nativeAvailable() const { return hotSwitch() || QFileInfo::exists(desktopClient()); }
// 发布Rhine 聊天界面桥接器。
void ChatBridge::publish(const WorkerEvent& event) {
    auto value = rhine::eventJson(event);
    const auto text = value.dump();
    emit events({QJsonDocument::fromJson(QByteArray::fromStdString(text)).object().toVariantMap()});
}

// 设置Rhine 聊天界面桥接器使用的配置。
void ChatBridge::setConfig(const QVariantMap& values) {
    static const QStringList keys{"host", "port", "serverName", "caFile", "dataDirectory", "username"};
    for (auto it = values.begin(); it != values.end(); ++it) if (keys.contains(it.key())) config_[it.key()] = it.value();
    emit configChanged();
}
// 把 Rhine 提交的 JSON 命令转为业务线程请求，返回参数或提交错误。
QString ChatBridge::command(const QVariantMap& request) {
    QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(request)).toJson(QJsonDocument::Compact);
    QString error;
    try {
        auto json = nlohmann::json::parse(bytes.constBegin(), bytes.constEnd());
        auto command = rhine::commandFromJson(json);
        const bool posted = commandDispatcher ? commandDispatcher(std::move(command)) : worker_->post(std::move(command));
        if (json.contains("password") && json["password"].is_string()) {
            auto& password = json["password"].get_ref<std::string&>(); OPENSSL_cleanse(password.data(), password.size());
        }
        if (!posted) error = QStringLiteral("业务队列已满，请稍后重试");
    } catch (...) { error = QStringLiteral("请求无效"); }
    OPENSSL_cleanse(bytes.data(), static_cast<std::size_t>(bytes.size()));
    if (ownedWorker_) drain();
    return error;
}

// 读出工作线程产生的客户端事件，转换为 Rhine 可用的数据。
void ChatBridge::drain() {
    QVariantList list;
    for (const auto& item : worker_->drain()) {
        const auto text = rhine::eventJson(item).dump();
        list.push_back(QJsonDocument::fromJson(QByteArray::fromStdString(text)).object().toVariantMap());
    }
    if (!list.isEmpty()) emit events(list);
}

// 返回Rhine 聊天界面桥接器使用的本机文件路径。
QString ChatBridge::localPath(const QUrl& url) const { return QDir::toNativeSeparators(url.toLocalFile()); }
// 保存Copy。
QString ChatBridge::saveCopy(const QString& source, const QUrl& target) const {
    const auto destination = target.toLocalFile();
    if (destination.isEmpty() || !QFileInfo(source).isFile()) return QStringLiteral("无法读取已验证文件");
    if (QFileInfo::exists(destination) && !QFile::remove(destination)) return QStringLiteral("目标文件无法覆盖");
    return QFile::copy(source, destination) ? QString() : QStringLiteral("保存失败");
}
// 显示位置Rhine 聊天界面桥接器。
void ChatBridge::reveal(const QString& path) const {
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
}
// 切换前端。
QString ChatBridge::switchFrontend(const QString& design) {
    if (frontendSwitch) return frontendSwitch(design);
    if (design != "codex" || !nativeAvailable()) return QStringLiteral("Codex 界面入口不可用");
    QStringList args{"--ui", design, "--host", config_["host"].toString(), "--port", config_["port"].toString(),
        "--server-name", config_["serverName"].toString(), "--ca", config_["caFile"].toString(),
        "--data-dir", config_["dataDirectory"].toString(), "--username", config_["username"].toString()};
    if (!config_["accountScope"].toString().isEmpty()) args << "--account-scope" << config_["accountScope"].toString();
    if (!QProcess::startDetached(desktopClient(), args, QCoreApplication::applicationDirPath())) return QStringLiteral("无法启动所选界面");
    QSettings preferences(QDir(config_["dataDirectory"].toString()).filePath("preferences.ini"), QSettings::IniFormat);
    preferences.setValue("ui/nativeFrontend", "codex"); preferences.sync();
    worker_->requestStop(); worker_->join();
    QCoreApplication::quit();
    return {};
}
// 返回Rhine 聊天界面桥接器使用的设置项。
QVariant ChatBridge::setting(const QString& key, const QVariant& fallback) const { return settings_.value(key, fallback); }
// 设置Rhine 聊天界面桥接器使用的设置项。
void ChatBridge::setSetting(const QString& key, const QVariant& value) { settings_.setValue(key, value); }
}
