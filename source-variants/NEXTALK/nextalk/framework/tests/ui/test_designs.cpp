#include "main_window.hpp"
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>

// The desktop client ships the Codex design only (GLM and the Qt Rhine port
// were removed). Saved GLM/Rhine/unknown preferences from older releases must
// still open Codex, and the old --ui launch option must not select another design.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir data;
    if (!data.isValid()) return 1;
    ccfw::gui::WindowOptions options;
    options.dataDirectory = std::filesystem::path(data.path().toStdWString());
    const auto output = qEnvironmentVariable("CCFW_DESIGN_SCREENSHOTS");
    if (!output.isEmpty()) QDir().mkpath(output);
    auto capture = [&](QWidget& window, const QString& name) {
        QTest::qWait(100);
        return output.isEmpty() || window.grab().save(output + "/" + name + ".png");
    };
    {
        ccfw::gui::MainWindow window(options); window.show();
        if (!QTest::qWaitForWindowExposed(&window)) return 2;
        if (window.property("frontendDesign").toString() != "codex" || !capture(window, "codex-account")) return 3;
        for (const char* removed : {"designSwitch", "designGLM", "designCodex", "designRhine", "rhineAbout", "rhineArchivePane"})
            if (window.findChild<QWidget*>(removed)) return 4;
        auto* tabs = window.findChild<QTabWidget*>();
        auto* message = window.findChild<QPlainTextEdit*>("chatInput");
        auto* attachment = window.findChild<QPushButton*>("fileTransferToggle");
        if (!tabs || !message || !attachment) return 5;
        tabs->setCurrentIndex(1); message->setPlainText(QStringLiteral("草稿"));
        attachment->setChecked(true);
        if (!capture(window, "codex-chat")) return 6;
        window.resize(860, 640); QTest::qWait(100);
        if (!capture(window, "codex-minimum")) return 7;
        for (auto* widget : {static_cast<QWidget*>(message), static_cast<QWidget*>(attachment)})
            if (!window.rect().contains(QRect(widget->mapTo(&window, QPoint{}), widget->size()))) return 8;
        tabs->setCurrentIndex(2); if (!capture(window, "codex-groups")) return 9;
    }
    for (const char* saved : {"glm", "rhine", "unknown-future-design"}) {
        QSettings settings(data.path() + "/preferences.ini", QSettings::IniFormat);
        settings.setValue("ui/design", saved); settings.sync();
        ccfw::gui::MainWindow restored(options);
        if (restored.property("frontendDesign").toString() != "codex") return 10;
    }
    for (const char* launch : {"glm", "rhine"}) {
        auto launchOptions = options; launchOptions.frontend = launch;
        ccfw::gui::MainWindow window(launchOptions);
        if (window.property("frontendDesign").toString() != "codex") return 11;
    }
    std::cout << "Codex is the only desktop design; GLM/Rhine preferences and launch options fall back to Codex.\n";
    return 0;
}
