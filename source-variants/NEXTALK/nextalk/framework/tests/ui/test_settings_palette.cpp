#include "main_window.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <map>
#include <memory>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
double luminance(const QColor& color) {
    const auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}
double contrast(const QColor& a, const QColor& b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
// Read the actual rendered row, including stylesheet subcontrol colors, rather
// than assuming a popup inherited the parent window's QPalette.
bool inspectRow(const QImage& image, const QRect& row, bool dark, bool disabled,
                const QString& name, QJsonArray& results) {
    const auto ratio = image.devicePixelRatio();
    const auto logical = row.adjusted(12, 3, -20, -3);
    const QRect pixels(qRound(logical.x() * ratio), qRound(logical.y() * ratio),
                       qRound(logical.width() * ratio), qRound(logical.height() * ratio));
    std::map<QRgb, int> colors;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x)
            if (image.rect().contains(x, y)) ++colors[image.pixel(x, y)];
    if (colors.empty()) return false;
    const auto background = QColor::fromRgb(std::max_element(colors.begin(), colors.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; })->first);
    QColor ink = background;
    int inkPixels = 0;
    for (const auto& [rgb, count] : colors) {
        const auto candidate = QColor::fromRgb(rgb);
        const auto distance = std::max({std::abs(candidate.red() - background.red()),
            std::abs(candidate.green() - background.green()), std::abs(candidate.blue() - background.blue())});
        if (distance > 24 && count > inkPixels) { ink = candidate; inkPixels = count; }
    }
    const auto value = contrast(ink, background);
    const bool shade = dark ? background.lightness() < 130 : background.lightness() > 170;
    const bool passed = shade && inkPixels >= 3 && value >= (disabled ? 3.0 : 4.5);
    results.append(QJsonObject{{"case", name}, {"background", background.name()}, {"ink", ink.name()},
        {"contrast", value}, {"disabled", disabled}, {"passed", passed}});
    return passed;
}
bool inspectMenu(QMenu& menu, QWidget& owner, bool dark, const QString& name,
                 const QString& output, QJsonArray& results) {
    if (!menu.isVisible()) menu.popup(owner.mapToGlobal(QPoint(90, 90)));
    QTest::qWait(35);
    QAction* first = nullptr;
    for (auto* action : menu.actions()) if (!action->isSeparator() && action->isVisible() && action->isEnabled()) { first = action; break; }
    if (!first) { menu.close(); return false; }
    bool okay = true;
    for (int state = 0; state < 3; ++state) {
        first->setEnabled(state != 2);
        menu.setActiveAction(state == 1 ? first : nullptr);
        QTest::qWait(20);
        const auto canvas = menu.grab();
        for (auto* action : menu.actions()) {
            if (!action->isSeparator() && action->isVisible())
                okay &= inspectRow(canvas.toImage(), menu.actionGeometry(action), dark, !action->isEnabled(),
                    name + QString("-state-%1-").arg(state) + action->text(), results);
        }
        if (!output.isEmpty()) okay &= canvas.save(output + "/" + name + QString("-state-%1.png").arg(state));
    }
    first->setEnabled(true); menu.close();
    return okay;
}
bool inspectPopups(ccfw::gui::MainWindow& window, QComboBox& skin, bool dark,
                   const QString& name, const QString& output, QJsonArray& results) {
    bool okay = true;
    int menuIndex = 0;
    for (auto* menu : window.findChildren<QMenu*>())
        okay &= inspectMenu(*menu, window, dark, name + QString("-menu-%1-").arg(menuIndex++) + menu->parent()->objectName(), output, results);
    if (menuIndex < 6) return false;
    auto* search = window.findChild<QLineEdit*>("recentSearch");
    auto* input = window.findChild<QPlainTextEdit*>("chatInput");
    auto* recent = window.findChild<QListWidget*>("recentConversations");
    if (!search || !input || !recent) return false;
    const auto previousSearch = search->text(), previousInput = input->toPlainText();
    search->setText(QStringLiteral("配色检查")); search->selectAll();
    input->setPlainText(QStringLiteral("配色检查")); input->selectAll();
    std::unique_ptr<QMenu> searchMenu(search->createStandardContextMenu());
    std::unique_ptr<QMenu> inputMenu(input->createStandardContextMenu());
    okay &= inspectMenu(*searchMenu, window, dark, name + "-line-edit", output, results);
    okay &= inspectMenu(*inputMenu, window, dark, name + "-text-edit", output, results);
    search->setText(previousSearch); input->setPlainText(previousInput);
    auto* item = new QListWidgetItem(QStringLiteral("配色回归会话"), recent);
    item->setData(Qt::UserRole, "peer:palette-check"); recent->doItemsLayout();
    bool inspected = false;
    QTimer capture;
    capture.setSingleShot(true);
    QObject::connect(&capture, &QTimer::timeout, &window, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu) { okay &= inspectMenu(*menu, window, dark, name + "-recent-context", output, results); inspected = true; }
    });
    capture.start(60);
    QMetaObject::invokeMethod(recent, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, recent->visualItemRect(item).center()));
    capture.stop(); delete item; okay &= inspected;
    skin.showPopup(); QTest::qWait(35);
    auto* view = skin.view();
    for (int state = 0; state < 2; ++state) {
        view->selectionModel()->setCurrentIndex(view->model()->index(state, 0), QItemSelectionModel::ClearAndSelect);
        QTest::qWait(20);
        const auto canvas = view->viewport()->grab();
        for (int i = 0; i < skin.count(); ++i)
            okay &= inspectRow(canvas.toImage(), view->visualRect(view->model()->index(i, 0)), dark, false,
                name + QString("-combo-state-%1-row-%2").arg(state).arg(i), results);
        if (!output.isEmpty()) okay &= canvas.save(output + "/" + name + QString("-combo-state-%1.png").arg(state));
    }
    skin.hidePopup();
    return okay;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const auto original = app.palette();
    const auto output = qEnvironmentVariable("CCFW_SETTINGS_EVIDENCE");
    if (!output.isEmpty()) QDir().mkpath(output);
    QJsonArray results;
    QJsonArray popupResults;
    bool okay = true;
    for (bool systemDark : {true, false}) {
        auto system = original;
        for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            system.setColor(group, QPalette::Window, QColor(systemDark ? "#2c2c2c" : "#f0f0f0"));
            system.setColor(group, QPalette::WindowText, QColor(systemDark ? "#f0f0f0" : "#202020"));
        }
        app.setPalette(system);
        QTemporaryDir profile;
        QSettings preferences(profile.path() + "/preferences.ini", QSettings::IniFormat);
        preferences.setValue("ui/colors/codex", "#467a9b"); preferences.sync();
        ccfw::gui::WindowOptions options;
        options.dataDirectory = std::filesystem::path(profile.path().toStdWString());
        options.frontend = "codex";
        ccfw::gui::MainWindow window(options); window.show();
        if (!QTest::qWaitForWindowExposed(&window)) return 2;
        auto* dialog = window.findChild<QDialog*>("settingsDialog");
        if (!dialog) return 3;
        auto* tabs = dialog->findChild<QTabWidget*>();
        auto* mode = dialog->findChild<QPushButton*>("appearanceMode");
        auto* skin = dialog->findChild<QComboBox*>("skinChoice");
        if (!tabs || tabs->count() != 5 || !mode || !skin) return 4;
        dialog->show();
        if (!QTest::qWaitForWindowExposed(dialog)) return 5;
        for (int selectedSkin : {0, 1}) {
            skin->setCurrentIndex(selectedSkin);
            for (int stage = 0; stage < 3; ++stage) {
                const bool dark = stage == 1;
                if (mode->isChecked() != dark) mode->click();
                for (int pageIndex = 0; pageIndex < tabs->count(); ++pageIndex) {
                    tabs->setCurrentIndex(pageIndex); QTest::qWait(40);
                    auto* page = tabs->currentWidget();
                    const auto canvas = dialog->grab();
                    const auto image = canvas.toImage();
                    const auto ratio = image.devicePixelRatio();
                    const auto point = page->mapTo(dialog, QPoint(page->width() - 8, page->height() - 8));
                    const auto background = image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
                    const bool shadeOkay = dark ? background.lightness() < 100 : background.lightness() > 170;
                    okay &= shadeOkay;
                    QJsonArray labels;
                    for (auto* label : page->findChildren<QLabel*>()) {
                        if (!label->isVisible() || label->text().isEmpty()) continue;
                        const auto ink = label->palette().color(label->foregroundRole());
                        const auto value = contrast(ink, background);
                        okay &= value >= 4.5;
                        labels.append(QJsonObject{{"text", label->text()}, {"ink", ink.name()}, {"contrast", value}});
                    }
                    const auto name = QString("os-%1-%2-%3-tab-%4").arg(systemDark ? "dark" : "light", selectedSkin ? "pencil" : "classic", dark ? "dark" : stage == 2 ? "light-restored" : "light").arg(pageIndex);
                    if (!output.isEmpty() && (systemDark || pageIndex == 0)) okay &= canvas.save(output + "/" + name + ".png");
                    results.append(QJsonObject{{"case", name}, {"background", background.name()}, {"shadeOkay", shadeOkay}, {"labels", labels}});
                }
                tabs->setCurrentIndex(0);
                const auto popupName = QString("os-%1-%2-%3").arg(systemDark ? "dark" : "light",
                    selectedSkin ? "pencil" : "classic", dark ? "dark" : stage == 2 ? "light-restored" : "light");
                okay &= inspectPopups(window, *skin, dark, popupName, output, popupResults);
            }
        }
        dialog->close(); window.close();
    }
    const auto json = QJsonDocument(results).toJson(QJsonDocument::Indented);
    if (!output.isEmpty()) { QFile file(output + "/palette-results.json"); if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) return 6; }
    if (!output.isEmpty()) {
        QFile file(output + "/popup-results.json");
        const auto bytes = QJsonDocument(popupResults).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 7;
    }
    std::cout << "Settings palette: " << results.size() << " page states, contrast >= 4.5, " << (okay ? "passed" : "FAILED") << ".\n";
    std::cout << "Popup palette: " << popupResults.size() << " rendered action/item states.\n";
    return okay ? 0 : 1;
}
