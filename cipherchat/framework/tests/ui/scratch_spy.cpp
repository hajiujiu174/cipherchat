// Temporary click-routing spy. Delete after diagnosing.
#include "main_window.hpp"
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTest>
#include <iostream>
class Spy : public QObject {
public:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* widget = static_cast<QWidget*>(obj);
            std::cout << "PRESS -> name='" << widget->objectName().toStdString() << "' class=" << widget->metaObject()->className() << "\n" << std::flush;
        }
        return false;
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ccfw::gui::WindowOptions options; options.dataDirectory = std::filesystem::path("scratch-spy-data");
    ccfw::gui::MainWindow window(options); window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000)) return 3;
    Spy spy; app.installEventFilter(&spy);
    auto* registerButton = window.findChild<QPushButton*>("registerButton");
    const auto center = registerButton->mapToGlobal(registerButton->rect().center());
    std::cout << "registerButton global center: " << center.x() << "," << center.y() << "\n" << std::flush;
    auto* childAt = window.childAt(window.mapFromGlobal(center));
    std::cout << "childAt center: name='" << (childAt ? childAt->objectName().toStdString() : std::string("<null>"))
        << "' class=" << (childAt ? childAt->metaObject()->className() : "") << "\n" << std::flush;
    QTest::mouseClick(registerButton, Qt::LeftButton);
    QTest::qWait(200);
    auto* banner = window.findChild<QLabel*>("notification");
    std::cout << "banner: '" << banner->text().toStdString() << "'\n" << std::flush;
    return banner->text() != QStringLiteral("输入账号和密码即可登录。新账号选择“注册并登录”。") ? 0 : 4;
}
