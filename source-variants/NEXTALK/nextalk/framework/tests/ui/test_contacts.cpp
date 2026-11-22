#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "qt_client_page.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <iostream>
using namespace ccfw;
namespace {
template<class T> T* control(QWidget& window, const char* name) {
    auto* result = window.findChild<T*>(name); REQUIRE(result); return result;
}
bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < 7000) { if (predicate()) return true; QTest::qWait(20); }
    return predicate();
}
QPushButton* action(QWidget& window, const char* name, const QString& user) {
    for (auto* value : window.findChildren<QPushButton*>(name)) if (value->property("username").toString() == user) return value;
    return nullptr;
}
}
TEST_CASE("M9-02 native windows discover request decline accept restore and isolate accounts") {
    test::RealServer server; const auto output = test::tempDirectory("M9-02-ui");
    gui::WindowOptions a; a.connection = server.options(); a.dataDirectory = output / "alice"; a.username = "alice";
    auto b = a; b.dataDirectory = output / "bob"; b.username = "bob";
    gui::MainWindow alice(a), bob(b); alice.show(); bob.show();
    REQUIRE(QTest::qWaitForWindowExposed(&alice)); REQUIRE(QTest::qWaitForWindowExposed(&bob));
    test::QtClientPage ap(alice), bp(bob);
    ap.authenticate("password123", true); bp.authenticate("password123", true);
    REQUIRE(waitFor([&] { return ap.online() && bp.online(); }));
    auto* ac = control<QListWidget>(alice, "contacts"); auto* bc = control<QListWidget>(bob, "contacts");
    CHECK(ac->count() == 0); CHECK(bc->count() == 0);
    control<QTabWidget>(alice, "mainTabs")->setCurrentIndex(1);
    control<QTabWidget>(bob, "mainTabs")->setCurrentIndex(1);
    auto* section = control<QComboBox>(alice, "directorySection"); section->setCurrentIndex(2);
    REQUIRE(waitFor([&] { return action(alice, "temporaryChat", "bob"); }));
    control<QLineEdit>(alice, "strangerQuery")->setText("missing"); control<QPushButton>(alice, "searchStrangers")->click();
    REQUIRE(waitFor([&] { return control<QListWidget>(alice, "strangers")->count() == 0; }));
    control<QLineEdit>(alice, "strangerQuery")->setText("bo"); control<QPushButton>(alice, "searchStrangers")->click();
    REQUIRE(waitFor([&] { return action(alice, "temporaryChat", "bob"); }));
    action(alice, "temporaryChat", "bob")->click();
    CHECK(control<QLineEdit>(alice, "chatPeer")->text() == "bob"); CHECK(section->currentIndex() == 1); CHECK(ac->count() == 0);
    CHECK(control<QListWidget>(alice, "temporaryChats")->count() == 1);
    section->setCurrentIndex(2); REQUIRE(waitFor([&] { return action(alice, "addFriend", "bob"); }));
    action(alice, "addFriend", "bob")->click();
    REQUIRE(waitFor([&] { auto* add = action(alice, "addFriend", "bob"); return action(bob, "declineFriend", "alice") && add && !add->isEnabled(); }));
    CHECK(ac->count() == 0); CHECK(bc->count() == 0);
    control<QComboBox>(bob, "directorySection")->setCurrentIndex(3);
    REQUIRE(bob.grab().save(QString::fromStdWString((output / "incoming.png").wstring())));
    action(bob, "declineFriend", "alice")->click();
    REQUIRE(waitFor([&] { auto* add = action(alice, "addFriend", "bob"); return add && add->isEnabled() && !action(bob, "acceptFriend", "alice"); }));
    action(alice, "addFriend", "bob")->click();
    REQUIRE(waitFor([&] { auto* add = action(alice, "addFriend", "bob"); return action(bob, "acceptFriend", "alice") && add && !add->isEnabled(); }));
    action(bob, "acceptFriend", "alice")->click();
    REQUIRE(waitFor([&] { return ac->count() == 1 && bc->count() == 1; }));
    CHECK(control<QListWidget>(alice, "temporaryChats")->count() == 0);
    section->setCurrentIndex(0); QTest::qWait(150); CHECK(ac->count() == 1);
    REQUIRE(alice.grab().save(QString::fromStdWString((output / "friends.png").wstring())));
    control<QTabWidget>(alice, "mainTabs")->setCurrentIndex(0); ap.authenticate("password123", false);
    REQUIRE(waitFor([&] { return ap.online() && ac->count() == 1; })); QTest::qWait(150); CHECK(ac->count() == 1);
    // Switching accounts clears the previous directory before any server response arrives.
    control<QLineEdit>(alice, "username")->setText("charlie"); ap.authenticate("password123", true);
    CHECK(ac->count() == 0);
    REQUIRE(waitFor([&] { return ap.online(); })); QTest::qWait(100); CHECK(ac->count() == 0);
    control<QTabWidget>(alice, "mainTabs")->setCurrentIndex(1); section->setCurrentIndex(2);
    control<QLineEdit>(alice, "strangerQuery")->clear(); control<QPushButton>(alice, "searchStrangers")->click();
    REQUIRE(waitFor([&] { return control<QListWidget>(alice, "strangers")->count() == 2; }));
    for (bool codex : {true}) for (bool dark : {false, true}) {
        auto* appearance = control<QPushButton>(alice, "appearanceMode"); if (appearance->isChecked() != dark) appearance->click();
        alice.resize(860, 640); QTest::qWait(150);
        const auto name = std::string(codex ? "codex" : "glm") + (dark ? "-dark.png" : "-light.png");
        REQUIRE(alice.grab().save(QString::fromStdWString((output / name).wstring())));
        auto* list = control<QListWidget>(alice, "strangers");
        for (int row = 0; row < list->count(); ++row) {
            auto* widget = list->itemWidget(list->item(row)); REQUIRE(widget);
            for (auto* btn : widget->findChildren<QPushButton*>()) {
                CHECK(widget->rect().contains(QRect(btn->mapTo(widget, QPoint{}), btn->size())));
                CHECK(btn->width() >= btn->fontMetrics().horizontalAdvance(btn->text()) + 8);
            }
        }
    }
    std::cout << "Contact GUI evidence: " << output.string() << '\n'; alice.close(); bob.close();
}
int main(int argc, char** argv) { QApplication app(argc, argv); return doctest::Context(argc, argv).run(); }
