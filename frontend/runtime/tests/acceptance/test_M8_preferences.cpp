#define DOCTEST_CONFIG_IMPLEMENT
#include "client_fixture.hpp"
#include "../../gui/main_window.hpp"
#include "../ui/qt_client_page.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QElapsedTimer>
#include <QTest>
#include <iostream>
// 验证用例“Obsolete direct transfer preference cannot restore the removed toggle”所描述的行为。
TEST_CASE("Obsolete direct transfer preference cannot restore the removed toggle") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    ccfw::test::RealServer server;
    QSettings settings(directory.path() + "/preferences.ini", QSettings::IniFormat);
    settings.setValue("files/allowDirect", true);
    settings.sync();
    ccfw::gui::WindowOptions options;
    options.dataDirectory = std::filesystem::path(directory.path().toStdWString());
    options.connection = server.options();
    options.username = "preferences_test";
    ccfw::gui::MainWindow window(options);
    CHECK(window.findChild<QWidget*>("allowDirectFiles") == nullptr);
}
// 准备测试进程和输入，运行该文件验证的用例并返回退出状态。
int main(int argc,char** argv) {QApplication app(argc,argv);return doctest::Context(argc,argv).run();}
