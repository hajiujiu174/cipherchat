#include "services/mockservice.h"
#ifdef MESSAGER_HAS_CIPHERCHAT
#include "services/cipherchatservice.h"
#include "gui/update_controller.hpp"
#endif
#include "ui/mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <QProcess>
#include <QPushButton>
#include <QLabel>
#include <functional>
#include <memory>

using namespace Messager;
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("MessagerPrototype");
    QCoreApplication::setApplicationName("Messager");
    QCoreApplication::setApplicationVersion("0.3.15");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCommandLineParser parser;
    parser.setApplicationDescription("CipherChat · Messager 桌面界面");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"capture", "保存真实 QWidget 渲染截图与尺寸检查，然后退出", "directory"});
    parser.addOption({"skin", "界面皮肤 default 或 pencil", "skin"});
    parser.addOption({"login", "启动时显示账号窗口"});
    parser.addOption({"demo", "使用独立的本地模拟数据"});
    parser.addOptions({{"host", "服务器地址", "host"},
                       {"port", "服务器端口", "port"},
                       {"ca", "信任的服务器证书", "path"},
                       {"server-name", "证书主机名", "name"},
                       {"data-dir", "账号数据根目录", "path"},
                       {"account-scope", "兼容账号服务器标识", "scope"},
                       {"username", "预填用户名", "username"},
                       {"relay-only", "文件仅使用服务器中转"}});
    parser.process(app);
    // Directly opening the original executable follows the same installed version pointer.
    if(!parser.isSet("demo")&&!parser.isSet("capture")&&QFile::exists(QCoreApplication::applicationDirPath()+"/current.txt")) {
        if(QProcess::startDetached(QCoreApplication::applicationDirPath()+"/cipherchat-launcher.exe",QCoreApplication::arguments().mid(1)))return 0;
    }
    if (parser.isSet("capture")) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::cleanPath(parser.value("capture") + "/../work/capture-settings"));
    }
    app.setWindowIcon(applicationIcon());
    app.setStyle(QStyleFactory::create("Fusion"));
    if(parser.isSet("skin")) {
        const auto skin=parser.value("skin");
        if(skin!="default"&&skin!="pencil")parser.showHelp(2);
        Theme::instance().setSkin(skin=="pencil"?Theme::PencilSkin:Theme::DefaultSkin);
    }
    Theme::instance().apply();
    std::unique_ptr<ChatService> service;
    bool demo = parser.isSet("demo") || parser.isSet("capture");
#ifdef MESSAGER_HAS_CIPHERCHAT
    if (!demo) {
        QJsonObject config{{"host", "127.0.0.1"},
                           {"port", 55220},
                           {"serverName", "localhost"},
                           {"caFile", QCoreApplication::applicationDirPath() + "/server-cert.pem"},
                           {"dataDirectory", qEnvironmentVariable("CIPHERCHAT_DATA_DIR",
                                                                  QStandardPaths::writableLocation(
                                                                      QStandardPaths::GenericDataLocation) +
                                                                      "/CipherChat/remote-server2")},
                           {"directEnabled", false}};
        QFile configFile(QCoreApplication::applicationDirPath() + "/connection.json");
        if (configFile.open(QIODevice::ReadOnly)) {
            auto saved = QJsonDocument::fromJson(configFile.readAll()).object();
            for (auto i = saved.begin(); i != saved.end(); ++i)
                if (i.key() != "password")
                    config[i.key()] = i.value();
        }
        const QList<QPair<QString, QString>> names = {{"host", "host"},
                                                      {"ca", "caFile"},
                                                      {"server-name", "serverName"},
                                                      {"data-dir", "dataDirectory"},
                                                      {"account-scope", "accountScope"},
                                                      {"username", "username"}};
        for (const auto& pair : names)
            if (parser.isSet(pair.first))
                config[pair.second] = parser.value(pair.first);
        if (parser.isSet("port"))
            config["port"] = parser.value("port").toInt();
        if (parser.isSet("relay-only"))
            config["directEnabled"] = false;
        service = std::make_unique<CipherChatService>(config);
    }
#else
    demo = true;
#endif
    if (demo)
        service = std::make_unique<MockService>();
    AppController controller(service.get());
    MainWindow window(&controller);
#ifdef MESSAGER_HAS_CIPHERCHAT
    if(!demo) {
        auto* entry=new QPushButton(&window);entry->hide();
        auto* updates=new ccfw::gui::UpdateController(&window,entry,{},{},true);
        QObject::connect(&window,&MainWindow::loginOpened,updates,[updates](QWidget* login){
            auto* button=login->findChild<QPushButton*>("loginUpdateButton");auto* status=login->findChild<QLabel*>("loginUpdateStatus");
            if(button)QObject::connect(button,&QPushButton::clicked,updates,[updates]{updates->show();});
            if(status){status->setText(updates->statusText().section('\n',0,0));QObject::connect(updates,&ccfw::gui::UpdateController::statusChanged,status,[status](const QString& text){status->setText(text.section('\n',0,0));});}
        });
    }
#endif
    if (demo) {
        window.show();
        if (parser.isSet("login"))
            QTimer::singleShot(120, &window, &MainWindow::showLogin);
    } else {
        window.showLogin();
    }
    if (parser.isSet("capture")) {
        QString dir = QDir(parser.value("capture")).absolutePath();
        QDir().mkpath(dir);
        auto* steps = new QVector<std::function<void()>>;
        auto save = [&window, dir](const QString& name) { window.grab().save(dir + "/" + name + ".png"); };
        steps->push_back([&] { Theme::instance().setMode(Theme::Light); });
        steps->push_back([save, &window] {
            save("01-chat-light");
            window.setDetailsVisible(true);
        });
        steps->push_back([save, &window] {
            save("02-details");
            window.setDetailsVisible(false);
            Theme::instance().setMode(Theme::Dark);
        });
        steps->push_back([save, &window] {
            save("03-chat-dark");
            window.showPage(1);
        });
        steps->push_back([save, &window] {
            save("04-contacts-dark");
            window.showPage(2);
        });
        steps->push_back([save, &window] {
            save("05-files-dark");
            Theme::instance().setMode(Theme::Light);
            window.showPage(3);
        });
        steps->push_back([save, &window] {
            save("06-notifications");
            window.showPage(4);
        });
        steps->push_back([save, &window] {
            save("07-settings");
            window.showPage(0);
            window.resize(960, 640);
        });
        steps->push_back([save, &window] {
            save("08-chat-960x640");
            window.resize(1440, 900);
        });
        steps->push_back([save, &window] {
            save("09-chat-1440x900");
            window.resize(1280, 800);
        });
        steps->push_back([&window, dir] {
            QTimer::singleShot(180, &window, [dir] {
                auto* dialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
                if (!dialog)
                    return;
                dialog->grab().save(dir + "/10-login.png");
                for (auto* b : dialog->findChildren<QPushButton*>())
                    if (b->text() == "注册") {
                        b->click();
                        break;
                    }
                QTimer::singleShot(180, dialog, [dialog, dir] {
                    dialog->grab().save(dir + "/11-register.png");
                    dialog->reject();
                });
            });
            window.showLogin();
        });
        steps->push_back([&window, dir] {
            QJsonObject result;
            result["qtVersion"] = qVersion();
            result["skin"] = Theme::instance().pencil()?"pencil":"default";
            result["networkConnected"] = false;
            result["minimumWidth"] = window.minimumWidth();
            result["minimumHeight"] = window.minimumHeight();
            QJsonArray bounds;
            for (auto* w : window.findChildren<QWidget*>()) {
                if (w->isVisible() && !w->objectName().isEmpty() && !w->isWindow()) {
                    auto r = QRect(w->mapTo(&window, QPoint()), w->size());
                    if (!window.rect().contains(r) && !qobject_cast<QScrollBar*>(w) &&
                        !w->inherits("QWidgetWindow"))
                        bounds.append(w->objectName());
                }
            }
            result["namedWidgetsOutsideWindow"] = bounds;
            QFile f(dir + "/capture-check.json");
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(result).toJson());
            QTimer::singleShot(100, qApp, &QApplication::quit);
        });
        auto* timer = new QTimer(&window);
        auto* i = new int(0);
        QObject::connect(timer, &QTimer::timeout, &window, [steps, i, timer] {
            if (*i < steps->size())
                (*steps)[(*i)++]();
            else {
                timer->stop();
                delete steps;
                delete i;
            }
        });
        timer->start(550);
    }
    return app.exec();
}
