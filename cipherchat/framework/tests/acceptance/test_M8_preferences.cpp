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
TEST_CASE("P2P preference persists after authenticated settings and command override stays temporary") {
    QTemporaryDir directory; REQUIRE(directory.isValid()); ccfw::test::RealServer server;
    ccfw::gui::WindowOptions options; options.dataDirectory = std::filesystem::path(directory.path().toStdWString()); options.connection=server.options(); options.username="preferences_test";
    {
        ccfw::gui::MainWindow first(options);
        first.show(); QApplication::processEvents(); ccfw::test::QtClientPage page(first);
        page.authenticate("password123",true);QElapsedTimer timer;timer.start();while(!page.online()&&timer.elapsed()<7000)QTest::qWait(20);REQUIRE(page.online());
        REQUIRE(page.directFilesEnabled());
        page.setDirectFilesEnabled(false);
        page.logout();
    }
    {
        ccfw::gui::MainWindow restarted(options);
        CHECK_FALSE(ccfw::test::QtClientPage(restarted).directFilesEnabled());
    }
    {
        options.directEnabled = true; ccfw::gui::MainWindow overridden(options);
        CHECK(ccfw::test::QtClientPage(overridden).directFilesEnabled());
    }
    options.directEnabled.reset();
    {
        ccfw::gui::MainWindow persisted(options);
        CHECK_FALSE(ccfw::test::QtClientPage(persisted).directFilesEnabled());
    }
    QSettings settings(directory.path() + "/preferences.ini", QSettings::IniFormat);
    CHECK_FALSE(settings.value("files/allowDirect", true).toBool());
    std::cout << "P2P preference persists across windows; command override does not overwrite it.\n";
}
int main(int argc,char** argv) {QApplication app(argc,argv);return doctest::Context(argc,argv).run();}
