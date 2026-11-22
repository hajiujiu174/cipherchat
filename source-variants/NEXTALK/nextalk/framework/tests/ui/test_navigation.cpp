#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "qt_client_page.hpp"
#include "main_window.hpp"
#include <QApplication>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <fstream>
#include <iostream>

TEST_CASE("Semantic actions survive renamed reordered and additional legacy tabs") {
    ccfw::test::RealServer server; const auto directory=ccfw::test::tempDirectory("QQ-navigation");
    ccfw::gui::WindowOptions options; options.connection=server.options(); options.dataDirectory=directory/"client"; options.username="navigation_user";
    ccfw::gui::MainWindow window(options);
    // Deliberately mutate the real UI to catch accidental dependence on the
    // historical three-tab count, Chinese captions, object name or ordering.
    auto* tabs = window.findChild<QTabWidget*>("mainTabs"); REQUIRE(tabs);
    tabs->setObjectName("redesignedNavigation");
    for (int i = 0; i < tabs->count(); ++i) tabs->setTabText(i, "Page " + QString::number(i));
    tabs->addTab(new QWidget, "Extra page");
    tabs->tabBar()->moveTab(0, 2); tabs->setCurrentIndex(tabs->count() - 1);
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    ccfw::test::QtClientPage page(window);
    page.authenticate("password123",true); QElapsedTimer timer;timer.start();while(!page.online()&&timer.elapsed()<7000)QTest::qWait(20); REQUIRE(page.online());
    const auto source=directory/"navigation-test.bin"; std::ofstream(source)<<"navigation selection";
    page.selectConversation("navigation_peer"); page.selectFile(QString::fromStdWString(source.wstring())); REQUIRE(page.canSendFile());
    page.showGroups(); page.selectConversation("another_peer");
    CHECK_FALSE(page.selectedFile().isEmpty()); auto* recipient=window.findChild<QLabel*>("attachmentRecipient"); REQUIRE(recipient); CHECK(recipient->text().contains("navigation_peer")); CHECK_FALSE(recipient->text().contains("another_peer"));
    page.setDirectFilesEnabled(false);
    CHECK_FALSE(page.directFilesEnabled());
    for(auto* name:{"contactsNav","filesNav","messagesNav"}) { auto* action=window.findChild<QPushButton*>(name); REQUIRE(action); REQUIRE(action->isVisible()); QTest::mouseClick(action,Qt::LeftButton); }
    page.logout(); window.close();
    std::cout << "Semantic actions survive renamed, reordered and additional tabs.\n";
}
int main(int argc,char** argv) { QApplication app(argc,argv);return doctest::Context(argc,argv).run(); }
