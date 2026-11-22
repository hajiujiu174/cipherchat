#include "main_window.hpp"
#include "color_picker.hpp"
#include "accent_theme.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir data;
    ccfw::gui::WindowOptions options; options.dataDirectory = std::filesystem::path(data.path().toStdWString());
    const auto output = qEnvironmentVariable("CCFW_DESIGN_SCREENSHOTS");
    bool okay = true;
    {
        ccfw::gui::MainWindow window(options); window.show();
        if (!QTest::qWaitForWindowExposed(&window)) return 1;
        auto edit = [&](const QString& hex, bool accept, bool save, bool reset, const QString& screenshot) {
            QTimer::singleShot(50, &window, [&, hex, accept, save, reset, screenshot] {
                auto* picker = window.findChild<QDialog*>("themeColorDialog");
                if (!picker) { okay = false; if (auto* modal = app.activeModalWidget()) modal->close(); return; }
                auto* input = picker->findChild<QLineEdit*>("colorHex");
                input->setFocus(); input->selectAll(); QTest::keyClicks(input, hex);
                okay &= window.property("themeAccent").toString() == hex.toLower();
                if (save) picker->findChild<QPushButton*>("saveCustomColor")->click();
                if (reset) picker->findChild<QPushButton*>("resetThemeColor")->click();
                if (!output.isEmpty() && !screenshot.isEmpty()) {
                    QTest::qWait(50);
                    QDir().mkpath(output); okay &= picker->grab().save(output + "/" + screenshot + ".png");
                }
                picker->findChild<QPushButton*>(accept ? "applyThemeColor" : "cancelThemeColor")->click();
            });
            window.findChild<QPushButton*>("chooseThemeColor")->click();
        };
        edit("#EF6C35", true, true, false, "color-picker");
        if (!okay || window.property("themeAccent").toString() != "#ef6c35") return 2;
        window.findChild<QTabWidget*>()->setCurrentIndex(1);
        auto* message = window.findChild<QPlainTextEdit*>("chatInput"); message->setPlainText(QStringLiteral("更换颜色也保留草稿"));
        edit("#000000", false, true, false, {});
        if (!okay || window.property("themeAccent").toString() != "#ef6c35" || message->toPlainText() != QStringLiteral("更换颜色也保留草稿")) return 3;
        if (!output.isEmpty()) { QTest::qWait(100); okay &= window.grab().save(output + "/orange-theme.png"); }
        if (ccfw::gui::theme::accentInk(Qt::white) != QColor(Qt::black) || ccfw::gui::theme::accentInk(Qt::black) != QColor(Qt::white)) return 5;
    }
    {
        ccfw::gui::MainWindow restored(options); restored.show();
        if (restored.property("themeAccent").toString() != "#ef6c35") return 7;
        QSettings settings(data.path() + "/preferences.ini", QSettings::IniFormat);
        if (settings.value("ui/customColors").toStringList() != QStringList{"#EF6C35"}) return 8;
        QTimer::singleShot(50, &restored, [&] {
            auto* picker = restored.findChild<QDialog*>("themeColorDialog");
            picker->findChild<QPushButton*>("resetThemeColor")->click();
            picker->findChild<QPushButton*>("applyThemeColor")->click();
        });
        restored.findChild<QPushButton*>("chooseThemeColor")->click();
        if (restored.property("themeAccent").toString() != "#6151c6") return 10;
    }
    // Exercise RGB/HSV synchronization, invalid HEX and the brightness control.
    ccfw::gui::ThemeColorPicker picker(QColor("#FF0000"), QColor("#6151C6"), {}, nullptr); picker.show();
    auto* mode = picker.findChild<QComboBox*>("colorMode"); mode->setCurrentIndex(1);
    picker.findChild<QSpinBox*>("colorChannel0")->setValue(120);
    if (picker.selectedColor().name() != "#00ff00") return 11;
    mode->setCurrentIndex(0); picker.findChild<QSpinBox*>("colorChannel2")->setValue(255);
    if (picker.selectedColor().name() != "#00ffff") return 12;
    picker.findChild<QSlider*>("colorBrightness")->setValue(0);
    if (picker.selectedColor().name() != "#000000") return 13;
    auto* input = picker.findChild<QLineEdit*>("colorHex"); input->selectAll(); QTest::keyClicks(input, "#12");
    if (picker.findChild<QPushButton*>("applyThemeColor")->isEnabled()) return 14;
    auto* spectrum = picker.findChild<QWidget*>("colorSpectrum");
    QTest::mouseClick(spectrum, Qt::LeftButton, Qt::NoModifier, QPoint(spectrum->width()/2, 2));
    picker.findChild<QSlider*>("colorBrightness")->setValue(255);
    if (picker.selectedColor().value() != 255 || picker.selectedColor().saturation() < 240) return 15;
    if (!okay) return 16;
    std::cout << "Color preview/cancel, HEX/RGB/HSV, swatches, Codex persistence, default reset and black/white contrast passed.\n";
    return 0;
}
