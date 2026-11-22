#include "main_window.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QDialog>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir data, otherData;
    auto osPalette=app.palette(); osPalette.setColor(QPalette::Window,QColor("#2c2c2c")); app.setPalette(osPalette);
    ccfw::gui::WindowOptions options; options.dataDirectory = std::filesystem::path(data.path().toStdWString());
    const auto output = qEnvironmentVariable("CCFW_DESIGN_SCREENSHOTS");
    if (!output.isEmpty()) QDir().mkpath(output);
    auto capture = [&](QWidget& widget, const QString& name) { QTest::qWait(80); return output.isEmpty() || widget.grab().save(output + "/" + name + ".png"); };
    {
        ccfw::gui::MainWindow window(options); window.show();
        if (!QTest::qWaitForWindowExposed(&window)) return 1;
        auto* mode = window.findChild<QPushButton*>("appearanceMode");
        auto* tabs = window.findChild<QTabWidget*>();
        auto* draft = window.findChild<QPlainTextEdit*>("chatInput");
        auto* peer = window.findChild<QLineEdit*>("chatPeer");
        if (!mode || mode->isChecked()) return 2;
        for (const auto* design : {"codex"}) {
            for (int value : {1, 0, 1}) {
                if (mode->isChecked() != bool(value)) QTest::mouseClick(mode, Qt::LeftButton);
                if (window.property("appearanceMode").toString() != (value ? "dark" : "light")) return 3;
                tabs->setCurrentIndex(0);
                if (!capture(window, QString(design) + (value ? "-dark-login" : "-light-login"))) return 4;
                tabs->setCurrentIndex(1); peer->setText("mode_peer"); draft->setPlainText(QStringLiteral("深浅切换保留草稿"));
                window.findChild<QPushButton*>("fileTransferToggle")->setChecked(true);
                QTest::mouseClick(mode, Qt::LeftButton);
                if (window.property("appearanceMode").toString() != (value ? "light" : "dark")) return 15;
                mode->setFocus(); QTest::keyClick(mode, Qt::Key_Space);
                if (draft->toPlainText() != QStringLiteral("深浅切换保留草稿") || peer->text() != "mode_peer" || tabs->currentIndex() != 1 || !window.findChild<QWidget*>("conversationFiles")->isVisible()) return 5;
                window.resize(860,640);
                if (!capture(window, QString(design) + (value ? "-dark-chat" : "-light-chat"))) return 6;
                const auto canvas=window.findChild<QWidget*>("conversationPanel")->grab().toImage();
                const auto shade=canvas.pixelColor(canvas.width()-1,0).lightness();
                if (value ? shade>100 : shade<180) return 17;
                if (!window.rect().contains(QRect(mode->mapTo(&window, QPoint{}), mode->size()))) return 7;
            }
        }
        // Previewing and cancelling an arbitrary accent must not change appearance.
        const auto accent = window.property("themeAccent").toString(); bool okay = false;
        QTimer::singleShot(50, &window, [&] {
            auto* picker = window.findChild<QDialog*>("themeColorDialog");
            auto* hex = picker->findChild<QLineEdit*>("colorHex"); hex->selectAll(); QTest::keyClicks(hex, "#FFFFFF");
            okay = window.property("appearanceMode").toString() == "dark" && window.property("themeAccent").toString() == "#ffffff";
            picker->findChild<QPushButton*>("cancelThemeColor")->click();
        });
        window.findChild<QPushButton*>("chooseThemeColor")->click();
        if (!okay || window.property("themeAccent").toString() != accent) return 8;
        tabs->setCurrentIndex(2); if (!capture(window, "dark-groups")) return 9;
        auto otherOptions = options; otherOptions.dataDirectory = std::filesystem::path(otherData.path().toStdWString());
        ccfw::gui::MainWindow other(otherOptions);
        if (other.property("appearanceMode").toString() != "light") return 10;
        QTest::mouseClick(mode, Qt::LeftButton);
        for (auto role : {QPalette::Window, QPalette::Base, QPalette::Text, QPalette::WindowText})
            if (window.palette().color(QPalette::Active, role) != other.palette().color(QPalette::Active, role)) return 11;
        QTest::mouseClick(mode, Qt::LeftButton);
    }
    {
        ccfw::gui::MainWindow restored(options);
        if (restored.property("appearanceMode").toString() != "dark") return 12;
        if (restored.findChild<QPushButton*>("appearanceMode")->text() != QStringLiteral("深色")) return 16;
        restored.findChild<QPushButton*>("appearanceMode")->click();
    }
    {
        ccfw::gui::MainWindow restored(options);
        if (restored.property("appearanceMode").toString() != "light") return 13;
    }
    QSettings settings(data.path() + "/preferences.ini", QSettings::IniFormat); settings.setValue("ui/appearance", "unknown"); settings.sync();
    ccfw::gui::MainWindow fallback(options);
    if (fallback.property("appearanceMode").toString() != "light") return 14;
    std::cout << "Appearance: Codex, drafts, attachments, accent cancellation, isolated windows, palette restoration and persisted modes passed.\n";
    return 0;
}
