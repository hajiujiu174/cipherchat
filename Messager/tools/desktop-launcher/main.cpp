#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
struct Frontend {
    QString id;
    QString title;
    QString description;
    QString program;
    QString directory;
    QStringList arguments;
};

QString absolutePath(const QString &path) {
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path));
}

QList<Frontend> readFrontends(const QJsonObject &config) {
    QList<Frontend> result;
    for (const auto &value : config.value("frontends").toArray()) {
        const auto item = value.toObject();
        Frontend frontend;
        frontend.id = item.value("id").toString();
        if (frontend.id != "messager" && frontend.id != "rhine")
            continue;
        frontend.title = item.value("title").toString();
        frontend.description = item.value("description").toString();
        frontend.program = absolutePath(item.value("program").toString());
        frontend.directory = absolutePath(item.value("workingDirectory").toString());
        for (const auto &argument : item.value("arguments").toArray())
            frontend.arguments.append(argument.toString());
        result.append(frontend);
    }
    return result;
}
bool launchFrontend(const Frontend& frontend, const QStringList& extraArguments = {}) {
    QProcess process;
    process.setProgram(frontend.program);
    process.setArguments(frontend.arguments + extraArguments);
    process.setWorkingDirectory(frontend.directory);
    if (process.startDetached())
        return true;
    QMessageBox::warning(nullptr, "无法打开 CipherChat", "启动失败：" + process.errorString());
    return false;
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("CipherChat 启动器");
    app.setFont(QFont("Microsoft YaHei UI", 10));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"choose", "Show the optional interface picker instead of opening Messager."});
    parser.addOption({"ui", "Open messager or rhine. The retired codex name redirects to messager.", "frontend", "messager"});
    parser.addOption({"check", "Write launch paths and availability to the given JSON file, without starting a client.", "output"});
    parser.addPositionalArgument("client-arguments", "Client arguments after --.", "[-- client-arguments...]");
    parser.process(app);

    QFile configFile(QCoreApplication::applicationDirPath() + "/desktop-frontends.json");
    if (!configFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(nullptr, "CipherChat", "找不到界面启动配置，请检查 desktop-frontends.json。");
        return 1;
    }
    const auto config = QJsonDocument::fromJson(configFile.readAll()).object();
    const auto frontends = readFrontends(config);
    if (frontends.isEmpty()) {
        QMessageBox::critical(nullptr, "CipherChat", "没有可用的界面选项，请检查启动配置。");
        return 1;
    }

    if (parser.isSet("check")) {
        QJsonArray entries;
        bool valid = true;
        for (const auto &frontend : frontends) {
            const bool available = QFileInfo::exists(frontend.program) && QDir(frontend.directory).exists();
            valid = valid && available;
            entries.append(QJsonObject{{"id", frontend.id}, {"program", frontend.program},
                                       {"workingDirectory", frontend.directory},
                                       {"arguments", QJsonArray::fromStringList(frontend.arguments)},
                                       {"available", available}});
        }
        QFile output(parser.value("check"));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        output.write(QJsonDocument(QJsonObject{{"valid", valid}, {"defaultFrontend", "messager"},
                                              {"retiredCodexRedirect", "messager"},
                                              {"frontends", entries}}).toJson());
        return valid ? 0 : 1;
    }

    const auto icon = QIcon(absolutePath(config.value("icon").toString()));
    app.setWindowIcon(icon);
    if (!parser.isSet("choose")) {
        QString selected = parser.value("ui");
        auto arguments = parser.positionalArguments();
        // Compatibility for old project scripts forwarding --ui after the separator.
        for (int i = 0; i < arguments.size();) {
            if (arguments[i] == "--ui" && i + 1 < arguments.size()) {
                selected = arguments[i + 1];
                arguments.removeAt(i);
                arguments.removeAt(i);
            } else if (arguments[i].startsWith("--ui=")) {
                selected = arguments.takeAt(i).mid(5);
            } else {
                ++i;
            }
        }
        if (selected == "codex")
            selected = "messager";
        for (const auto& frontend : frontends)
            if (frontend.id == selected)
                return launchFrontend(frontend, arguments) ? 0 : 1;
        QMessageBox::warning(nullptr, "CipherChat", "没有找到指定的界面入口。");
        return 1;
    }
    QWidget window;
    window.setWindowTitle("CipherChat · 选择界面");
    window.setObjectName("selector");
    window.setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    window.resize(560, 390);
    window.setMinimumSize(500, 370);
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(32, 28, 32, 26);
    layout->setSpacing(14);
    auto *title = new QLabel("打开 CipherChat");
    title->setObjectName("title");
    layout->addWidget(title);
    auto *subtitle = new QLabel("Messager 为默认界面，也可选择 Rhine");
    subtitle->setObjectName("subtitle");
    layout->addWidget(subtitle);
    layout->addSpacing(8);

    const bool dark = app.palette().color(QPalette::Window).lightness() < 128;
    const QString background = dark ? "#1c1e28" : "#f7f7fb";
    const QString foreground = dark ? "#f5f3ff" : "#242338";
    const QString secondary = dark ? "#b7b5c8" : "#77738b";
    const QString card = dark ? "#262837" : "#ffffff";
    const QString border = dark ? "#454457" : "#dedce9";
    window.setStyleSheet(QString(
        "QWidget#selector { background: %1; color: %2; }"
        "QLabel { color: %2; background: transparent; }"
        "QLabel#title { font-size: 27px; font-weight: 600; }"
        "QLabel#subtitle, QLabel#footer { color: %3; font-size: 13px; }"
        "QPushButton { text-align: left; padding: 17px 22px; border: 1px solid %5;"
        " border-radius: 12px; background: %4; color: %2; font-size: 15px; }"
        "QPushButton:hover { border: 1px solid #8872e8; }"
        "QPushButton:focus { border: 2px solid #8872e8; padding: 16px 21px; }"
        "QPushButton[primary=true] { background: #6250c8; color: white; border-color: #6250c8; }"
        "QPushButton[primary=true]:hover { background: #705ed5; }"
        "QPushButton:disabled { color: %3; }").arg(background, foreground, secondary, card, border));

    for (const auto &frontend : frontends) {
        auto *button = new QPushButton(frontend.title + "\n" + frontend.description);
        button->setObjectName("launch_" + frontend.id);
        button->setAccessibleName(frontend.title);
        button->setAccessibleDescription(frontend.description);
        button->setMinimumHeight(82);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("primary", frontend.id == "messager");
        const bool available = QFileInfo::exists(frontend.program) && QDir(frontend.directory).exists();
        button->setEnabled(available);
        if (!available) button->setToolTip("启动文件不存在：" + frontend.program);
        QObject::connect(button, &QPushButton::clicked, &window, [frontend, &window] {
            if (launchFrontend(frontend)) {
                window.close();
            }
        });
        layout->addWidget(button);
    }
    layout->addStretch();
    auto *footer = new QLabel("桌面和开始菜单默认直接打开 Messager。");
    footer->setObjectName("footer");
    layout->addWidget(footer);
    window.show();
    return app.exec();
}
