#include "main_window.hpp"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
template <typename T> T* control(QObject& window, const char* name) {
    auto* value = window.findChild<T*>(name); check(value != nullptr, name); return value;
}
void click(QObject& window, const char* name) { QTest::mouseClick(control<QPushButton>(window, name), Qt::LeftButton); }
void fits(QWidget& window, QWidget* widget) {
    check(widget->isVisible(), "control must remain visible");
    check(window.rect().contains(QRect(widget->mapTo(&window, QPoint{}), widget->size())), "control clipped at minimum size");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir data;
    const auto output = qEnvironmentVariable("CCFW_DESIGN_SCREENSHOTS");
    if (!output.isEmpty()) QDir().mkpath(output);
    auto capture = [&](QWidget& window, const QString& name) {
        QTest::qWait(180);
        check(output.isEmpty() || window.grab().save(output + '/' + name + ".png"), "screenshot failed");
    };
    ccfw::gui::WindowOptions options; options.dataDirectory = std::filesystem::path(data.path().toStdWString());
    options.frontend = "rhine";
    try {
        {
            ccfw::gui::MainWindow window(options); window.resize(1220, 800); window.show();
            check(QTest::qWaitForWindowExposed(&window), "window not exposed");
            check(window.property("frontendDesign") == "rhine", "launch override missing");
            auto* view = control<QQuickWidget>(window, "rhineArchiveView");
            const bool loaded = QTest::qWaitFor([&] { return view->rootObject() && view->rootObject()->property("modelLoaded").toBool(); }, 12000);
            if (!loaded) {
                capture(window, "rhine-load-failure");
                std::cerr << "QML status=" << view->status() << " source=" << view->source().toString().toStdString()
                    << " visible=" << view->isVisible() << " error="
                    << (view->rootObject() ? view->rootObject()->property("modelError").toString().toStdString() : "no QML root") << '\n';
                std::cerr << control<QWidget>(window, "rhineArchivePane")->property("sceneError").toString().toStdString() << '\n';
            }
            check(loaded, "upstream GLB did not load");
            auto* scene = view->rootObject();
            check(scene->property("sceneActive").toBool(), "scene not active");
            auto rendered = view->grabFramebuffer(); std::set<QRgb> colors;
            for (int y = 0; y < rendered.height(); y += 5) for (int x = 0; x < rendered.width(); x += 5) colors.insert(rendered.pixel(x, y));
            check(colors.size() > 80, "3D framebuffer is blank");
            capture(window, "rhine-light-account");
            const int row = scene->property("selectedRow").toInt();
            click(window, "rhineNext"); check(scene->property("selectedRow").toInt() == (row + 1) % 15, "next file failed");
            click(window, "rhinePrevious"); check(scene->property("selectedRow").toInt() == row, "previous file failed");
            click(window, "rhineInspect"); check(scene->property("inspecting").toBool(), "model inspect failed");
            QTest::qWait(850);
            const QPoint middle(view->width() / 2, view->height() / 2);
            QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, middle);
            QTest::mouseMove(view, middle + QPoint(60, 25), 30);
            QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, middle + QPoint(60, 25));
            check(scene->property("yaw").toDouble() != 0, "drag rotation failed");
            capture(window, "rhine-model");
            click(window, "rhineInspect");
            click(window, "rhineMotion");
            const int paused = scene->property("renderedTicks").toInt(); QTest::qWait(150);
            check(scene->property("renderedTicks").toInt() == paused, "paused scene still animates");
            click(window, "appearanceMode");
            check(scene->property("darkMode").toBool(), "3D dark mode missing");
            capture(window, "rhine-dark-account");
            // Real keyboard switch keeps the required label inside its pill.
            auto* appearance = control<QPushButton>(window, "appearanceMode");
            appearance->setFocus(); QTest::keyClick(appearance, Qt::Key_Space);
            check(appearance->text() == QStringLiteral("浅色"), "pill label incorrect");
            bool previewOkay = false;
            QTimer::singleShot(70, &window, [&] {
                auto* picker = control<QDialog>(window, "themeColorDialog");
                auto* hex = control<QLineEdit>(*picker, "colorHex"); hex->selectAll(); QTest::keyClicks(hex, "#278BA8");
                previewOkay = window.property("themeAccent").toString() == "#278ba8" && scene->property("accent").value<QColor>().name() == "#278ba8";
                click(*picker, "applyThemeColor");
            });
            click(window, "chooseThemeColor"); check(previewOkay, "accent not synchronized with scene");
            click(window, "rhineMotion"); QTest::qWait(150);
            check(scene->property("renderedTicks").toInt() > paused, "resume animation failed");
            auto* tabs = control<QTabWidget>(window, "mainTabs"); tabs->setCurrentIndex(1);
            check(!scene->property("sceneActive").toBool(), "hidden scene still active");
            const int hidden = scene->property("renderedTicks").toInt(); QTest::qWait(150);
            check(scene->property("renderedTicks").toInt() == hidden, "hidden scene timer running");
            auto* draft = control<QPlainTextEdit>(window, "chatInput");
            control<QLineEdit>(window, "chatPeer")->setText("rhine_peer"); draft->setPlainText(QStringLiteral("跨界面保留的真实草稿"));
            click(window, "fileTransferToggle");
            control<QLineEdit>(window, "filePath")->setText("D:/draft-attachment.txt");
            for (const auto* name : {"designGLM", "designCodex", "designRhine"}) {
                click(window, name);
                check(tabs->currentIndex() == 1 && draft->toPlainText() == QStringLiteral("跨界面保留的真实草稿"), "draft changed during design switch");
                check(control<QLineEdit>(window, "filePath")->text() == "D:/draft-attachment.txt", "attachment lost");
                check(control<QPushButton>(window, "fileTransferToggle")->isChecked(), "attachment folded");
            }
            check(window.property("themeAccent").toString() == "#278ba8", "Rhine accent not restored");
            capture(window, "rhine-custom-chat");
            window.resize(860, 640); QTest::qWait(200);
            for (const char* name : {"designRhine", "appearanceMode", "chatInput", "sendMessage", "fileTransferToggle"}) fits(window, control<QWidget>(window, name));
            capture(window, "rhine-minimum-chat");
            tabs->setCurrentIndex(0); QTest::qWait(200);
            for (const char* name : {"username", "password", "loginButton", "advancedSettingsToggle"}) fits(window, control<QWidget>(window, name));
            capture(window, "rhine-minimum-account");
            click(window, "advancedSettingsToggle");
            QTest::qWait(120);
            fits(window, control<QWidget>(window, "loginButton"));
            check(control<QWidget>(window, "advancedSettingsScroll")->height() >= 152, "connection settings viewport too small");
            capture(window, "rhine-minimum-settings");
            click(window, "advancedSettingsToggle");
            tabs->setCurrentIndex(2); capture(window, "rhine-groups");
            click(window, "rhineAbout");
            auto* about = control<QDialog>(window, "rhineAboutDialog");
            check(about->findChild<QPlainTextEdit*>()->toPlainText().contains("Copyright (c) 2026 LBEILC"), "MIT attribution absent");
            click(*about, "closeRhineAbout");
            tabs->setCurrentIndex(0); click(window, "rhineMotion"); click(window, "appearanceMode");
        }
        options.frontend.clear();
        {
            ccfw::gui::MainWindow restored(options);
            check(restored.property("frontendDesign") == "rhine", "Rhine preference not persisted");
            check(restored.property("appearanceMode") == "dark", "appearance not restored");
            check(restored.property("themeAccent") == "#278ba8", "accent not persisted");
            check(control<QPushButton>(restored, "rhineMotion")->isChecked(), "motion preference not persisted");
        }
        options.frontend = "glm";
        ccfw::gui::MainWindow overridden(options);
        check(overridden.property("frontendDesign") == "glm", "launch override failed");
        QSettings saved(data.path() + "/preferences.ini", QSettings::IniFormat);
        check(saved.value("ui/design") == "rhine", "launch override overwrote user preference");
        std::cout << "Rhine: GLB loaded and rendered, rotation, array controls, pause/resume, hidden scene idle, three-design draft/attachment preservation, theme color, light/dark pill, minimum layout, MIT attribution and restart preferences passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
