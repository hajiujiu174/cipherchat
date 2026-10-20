#include "archive_field.hpp"
#include "chat_bridge.hpp"
#include "../../update/update.hpp"
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <iostream>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtQuick3D/qquick3d.h>

// Native Rhine client: the RhineLabUI interaction model reimplemented with
// Qt Quick and Qt Quick 3D, backed by the same business worker as other UIs.
int main(int argc, char** argv) {
    Q_INIT_RESOURCE(rhine_native);
    QSurfaceFormat::setDefaultFormat(QQuick3D::idealSurfaceFormat(4));
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("CipherChat Rhine"); QCoreApplication::setOrganizationName("CipherChat");
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("CipherChat Rhine 原生界面")); parser.addHelpOption();
    parser.addOptions({{"host", QStringLiteral("服务器地址"), "host"}, {"port", QStringLiteral("服务器端口"), "port"},
        {"ca", QStringLiteral("信任证书路径"), "path"}, {"server-name", QStringLiteral("证书中的主机名"), "name"},
        {"account-scope", QStringLiteral("原账号服务器标识"), "id"}, {"data-dir", QStringLiteral("本地数据根目录"), "path"},
        {"username", QStringLiteral("预填用户名"), "username"}, {"relay-only", QStringLiteral("仅使用服务器中转文件")},
        {"skip-opening", QStringLiteral("跳过开场动画")},
        {"capture", QStringLiteral("验收用：按脚本操作界面并保存截图到目录"), "directory"},
        {"frame", QStringLiteral("验收用：冻结开场于指定秒数，截图后退出"), "seconds"}});
    parser.process(app);
    const QDir directory(QCoreApplication::applicationDirPath());
    QString host = "2603:1040:f02:d::21d", serverName; int port = 55220;
    if (directory.exists("update-config.json")) {
        try {
            const auto config = ccfw::updates::readJson(std::filesystem::path(directory.filePath("update-config.json").toStdWString()));
            host = QString::fromStdString(config.at("host").get<std::string>());
            port = config.at("port").get<int>();
            serverName = QString::fromStdString(config.at("server_name").get<std::string>());
        } catch (...) { return 3; }
    }
    if (parser.isSet("host")) host = parser.value("host");
    if (parser.isSet("port")) { bool ok = false; port = parser.value("port").toInt(&ok); if (!ok || port < 1 || port > 65535) return 2; }
    if (parser.isSet("server-name")) serverName = parser.value("server-name");
    if (serverName.isEmpty()) serverName = host;
    const QString dataDirectory = parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/CipherChat/remote-server2";
    QDir().mkpath(dataDirectory);
    const QVariantMap config{{"host", host}, {"port", port}, {"serverName", serverName},
        {"caFile", QDir::toNativeSeparators(QDir(parser.isSet("ca") ? parser.value("ca") : directory.filePath("server-cert.pem")).absolutePath())},
        {"dataDirectory", QDir::toNativeSeparators(dataDirectory)}, {"username", parser.value("username")},
        {"accountScope", parser.value("account-scope")}, {"directEnabled", !parser.isSet("relay-only")}};

    const auto families = QFontDatabase::families();
    const QString font = families.contains("MiSans") ? "MiSans" : families.contains("HarmonyOS Sans SC") ? "HarmonyOS Sans SC" : "Microsoft YaHei UI";
    QGuiApplication::setFont(QFont(font, 10));
    qmlRegisterType<ccfw::gui::ArchiveField>("Rhine", 1, 0, "ArchiveField");
    ccfw::gui::ChatBridge chat(config);
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) std::cerr << warning.toString().toStdString() << '\n';
    });
    engine.rootContext()->setContextProperty("chat", &chat);
    engine.rootContext()->setContextProperty("embeddedHotSwitch", false);
    engine.rootContext()->setContextProperty("uiFont", font);
    // Only the regular MiSans face is normally installed; Qt cannot read the web build's WOFF2 subsets.
    engine.rootContext()->setContextProperty("boldFont", QFontDatabase::styles(font).contains("Bold") ? font
        : families.contains("HarmonyOS Sans SC") ? QStringLiteral("HarmonyOS Sans SC") : font);
    engine.rootContext()->setContextProperty("skipOpening", parser.isSet("skip-opening"));
    engine.rootContext()->setContextProperty("captureDir", parser.isSet("capture") ? QUrl::fromLocalFile(QDir(parser.value("capture")).absolutePath()).toString() : QString());
    // Acceptance tuning: RHINE_TUNE=<json> overrides scene lighting constants without a rebuild.
    QVariantMap tune;
    if (const auto path = qEnvironmentVariable("RHINE_TUNE"); !path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) tune = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    }
    engine.rootContext()->setContextProperty("tune", tune);
    engine.rootContext()->setContextProperty("freezeAt", parser.isSet("frame") ? parser.value("frame").toDouble() : -1.0);
    engine.load(QUrl("qrc:/rhine_native/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
