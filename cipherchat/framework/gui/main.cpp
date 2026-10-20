#include "main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QPushButton>
#ifdef CCFW_HAS_RHINE_NATIVE
#include <QtQuick3D/qquick3d.h>
#endif
#include "../update/update.hpp"
int main(int argc, char** argv) {
#ifdef CCFW_HAS_RHINE_NATIVE
    QSurfaceFormat::setDefaultFormat(QQuick3D::idealSurfaceFormat(4));
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("CipherChat"); QCoreApplication::setOrganizationName("CipherChat");
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("CipherChat 端到端加密聊天")); parser.addHelpOption();
    parser.addOption({"relay-only", QStringLiteral("本次使用服务器中转，不创建文件直连监听")});
    parser.addOption({"ui", QStringLiteral("选择 Codex 或 Qt Rhine 复刻界面：codex|rhine"), "design"});
    parser.addOptions({{"host", QStringLiteral("服务器地址"), "host", "localhost"},
        {"port", QStringLiteral("服务器端口"), "port", "55220"}, {"ca", QStringLiteral("信任证书路径"), "path"},
        {"server-name", QStringLiteral("证书中的主机名"), "name", "localhost"},
        {"account-scope", QStringLiteral("迁移时保留的原账号服务器标识"), "id"},
        {"data-dir", QStringLiteral("本地数据根目录"), "path"}, {"username", QStringLiteral("预填用户名"), "username"}});
    parser.process(app); ccfw::gui::WindowOptions options;
    options.frontend = parser.value("ui").toStdString();
    if (!options.frontend.empty() && options.frontend != "glm" && options.frontend != "codex" && options.frontend != "rhine") return 2;
    if (parser.isSet("relay-only")) options.directEnabled = false;
    options.connection.host = parser.value("host").toStdString(); bool validPort = false; const int port = parser.value("port").toInt(&validPort);
    if (!validPort || port < 1 || port > 65535) return 2;
    options.connection.port = static_cast<std::uint16_t>(port);
    options.connection.caFile = std::filesystem::path(parser.value("ca").toStdWString());
    options.connection.expectedServerName = parser.value("server-name").toStdString();
    options.accountServerId = parser.value("account-scope").toStdString();
    if (options.accountServerId.size() > 512) return 2;
    options.username = parser.value("username").toStdString(); options.dataDirectory = std::filesystem::path(parser.value("data-dir").toStdWString());
    const auto directory = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString());
    if (std::filesystem::exists(directory / "update-config.json")) {
        try {
            const auto config = ccfw::updates::readJson(directory / "update-config.json");
            if (!parser.isSet("host")) options.connection.host = config.at("host").get<std::string>();
            if (!parser.isSet("port")) options.connection.port = config.at("port").get<std::uint16_t>();
            if (!parser.isSet("server-name")) options.connection.expectedServerName = config.at("server_name").get<std::string>();
            if (!parser.isSet("ca")) options.connection.caFile = directory / "server-cert.pem";
            if (!parser.isSet("data-dir")) options.dataDirectory = std::filesystem::path((QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/CipherChat/remote-server2").toStdWString());
        } catch (...) { return 3; }
    }
    const QString dataDirectory = options.dataDirectory.empty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/CipherChat/remote-server2"
        : QString::fromStdWString(options.dataDirectory.wstring());
    QSettings preferences(QDir(dataDirectory).filePath("preferences.ini"), QSettings::IniFormat);
    const QString selected = parser.isSet("ui") ? parser.value("ui")
        : preferences.value("ui/nativeFrontend", "rhine").toString();
    ccfw::gui::MainWindow window(std::move(options));
    window.show();
    if (selected == "rhine") QTimer::singleShot(0, &window, [&window] {
        const auto error = window.switchFrontend("rhine");
        if (!error.isEmpty()) QMessageBox::warning(&window, QStringLiteral("CipherChat"), error);
    });
    return app.exec();
}
