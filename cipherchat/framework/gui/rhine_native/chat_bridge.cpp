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
QString desktopClient() { return QDir(QCoreApplication::applicationDirPath()).filePath("cipherchat.exe"); }
}
ChatBridge::ChatBridge(QVariantMap config, QObject* parent)
    : QObject(parent), ownedWorker_(std::make_unique<BusinessWorker>()), worker_(ownedWorker_.get()), config_(std::move(config)),
      settings_(QDir(config_.value("dataDirectory").toString()).filePath("rhine-native.ini"), QSettings::IniFormat) {
    pump_.setInterval(60);
    connect(&pump_, &QTimer::timeout, this, &ChatBridge::drain);
    pump_.start();
}
ChatBridge::ChatBridge(QVariantMap config, BusinessWorker& worker, QObject* parent)
    : QObject(parent), worker_(&worker), config_(std::move(config)),
      settings_(QDir(config_.value("dataDirectory").toString()).filePath("rhine-native.ini"), QSettings::IniFormat) {}
ChatBridge::~ChatBridge() { if (ownedWorker_) { worker_->requestStop(); worker_->join(); } }
bool ChatBridge::nativeAvailable() const { return hotSwitch() || QFileInfo::exists(desktopClient()); }
void ChatBridge::publish(const WorkerEvent& event) {
    auto value = rhine::eventJson(event);
    const auto text = value.dump();
    emit events({QJsonDocument::fromJson(QByteArray::fromStdString(text)).object().toVariantMap()});
}

void ChatBridge::setConfig(const QVariantMap& values) {
    static const QStringList keys{"host", "port", "serverName", "caFile", "dataDirectory", "username", "directEnabled"};
    for (auto it = values.begin(); it != values.end(); ++it) if (keys.contains(it.key())) config_[it.key()] = it.value();
    emit configChanged();
}
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

void ChatBridge::drain() {
    QVariantList list;
    for (const auto& item : worker_->drain()) {
        const auto text = rhine::eventJson(item).dump();
        list.push_back(QJsonDocument::fromJson(QByteArray::fromStdString(text)).object().toVariantMap());
    }
    if (!list.isEmpty()) emit events(list);
}

QString ChatBridge::localPath(const QUrl& url) const { return QDir::toNativeSeparators(url.toLocalFile()); }
QString ChatBridge::saveCopy(const QString& source, const QUrl& target) const {
    const auto destination = target.toLocalFile();
    if (destination.isEmpty() || !QFileInfo(source).isFile()) return QStringLiteral("无法读取已验证文件");
    if (QFileInfo::exists(destination) && !QFile::remove(destination)) return QStringLiteral("目标文件无法覆盖");
    return QFile::copy(source, destination) ? QString() : QStringLiteral("保存失败");
}
void ChatBridge::reveal(const QString& path) const {
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
}
QString ChatBridge::switchFrontend(const QString& design) {
    if (frontendSwitch) return frontendSwitch(design);
    if (design != "codex" || !nativeAvailable()) return QStringLiteral("Codex 界面入口不可用");
    QStringList args{"--ui", design, "--host", config_["host"].toString(), "--port", config_["port"].toString(),
        "--server-name", config_["serverName"].toString(), "--ca", config_["caFile"].toString(),
        "--data-dir", config_["dataDirectory"].toString(), "--username", config_["username"].toString()};
    if (!config_["accountScope"].toString().isEmpty()) args << "--account-scope" << config_["accountScope"].toString();
    if (!config_["directEnabled"].toBool()) args << "--relay-only";
    if (!QProcess::startDetached(desktopClient(), args, QCoreApplication::applicationDirPath())) return QStringLiteral("无法启动所选界面");
    QSettings preferences(QDir(config_["dataDirectory"].toString()).filePath("preferences.ini"), QSettings::IniFormat);
    preferences.setValue("ui/nativeFrontend", "codex"); preferences.sync();
    worker_->requestStop(); worker_->join();
    QCoreApplication::quit();
    return {};
}
QVariant ChatBridge::setting(const QString& key, const QVariant& fallback) const { return settings_.value(key, fallback); }
void ChatBridge::setSetting(const QString& key, const QVariant& value) { settings_.setValue(key, value); }
}
