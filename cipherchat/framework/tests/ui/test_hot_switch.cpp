#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "qt_client_page.hpp"
#include "rhine_native/chat_bridge.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSValue>
#include <QTest>
#include <QtQuick3D/qquick3d.h>
#include <fstream>
#include <iostream>
using namespace ccfw;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 12000) {
    QElapsedTimer time; time.start();
    while (time.elapsed() < timeout) { if (predicate()) return true; QTest::qWait(20); }
    return predicate();
}
QQuickWindow* nativeWindow() {
    for (auto* window : QGuiApplication::allWindows())
        if (window->objectName() == "rhineHotWindow") return qobject_cast<QQuickWindow*>(window);
    return nullptr;
}
QVariantMap state(QQuickWindow* window) {
    QVariant result;
    REQUIRE(QMetaObject::invokeMethod(window, "sessionState", Q_RETURN_ARG(QVariant, result)));
    return result.canConvert<QJSValue>() ? result.value<QJSValue>().toVariant().toMap() : result.toMap();
}
}
TEST_CASE("Hot switching keeps an authenticated TLS session, drafts and file delivery") {
    test::RealServer server; const auto output = test::tempDirectory("hot-switch");
    gui::WindowOptions options; options.connection = server.options(); options.dataDirectory = output / "alice"; options.username = "hot_alice";
    auto second = options; second.dataDirectory = output / "bob"; second.username = "hot_bob";
    gui::MainWindow alice(options), bob(second); alice.show(); bob.show();
    test::QtClientPage ap(alice), bp(bob);
    ap.authenticate("password123", true); bp.authenticate("password123", true);
    REQUIRE(waitFor([&] { return ap.online() && bp.online(); }));
    CHECK(alice.findChild<QTabWidget*>("mainTabs")->currentIndex() == 1);
    ap.selectConversation("hot_bob"); bp.selectConversation("hot_alice");
    alice.findChild<QPushButton*>("inspectIdentity")->click();
    bob.findChild<QPushButton*>("inspectIdentity")->click();
    REQUIRE(waitFor([&] { return ap.identityConfirmationPending() && bp.identityConfirmationPending(); }));
    ap.openPeerSecurity(); bp.openPeerSecurity();
    ap.confirmIdentity(); bp.confirmIdentity();
    ap.sendMessage("before-hot-switch");
    REQUIRE(waitFor([&] { return bp.hasMessage("before-hot-switch") && ap.hasReceipt("before-hot-switch"); }));
    ap.sendMessage("Second message\nA second line that must occupy its own height.");
    ap.sendMessage("Third message with a long path: C:/Users/test/Desktop/CipherChat/example/long-file-name-for-layout-verification.txt");
    REQUIRE(waitFor([&] { return bp.hasMessage("Second message\nA second line that must occupy its own height."); }));
    auto* draft = alice.findChild<QPlainTextEdit*>("chatInput"); REQUIRE(draft);
    draft->setPlainText("draft survives both views");
    auto* switchButton = alice.findChild<QPushButton*>("switchQtRhine"); REQUIRE(switchButton); switchButton->click();
    REQUIRE(waitFor([&] { return nativeWindow() && nativeWindow()->isVisible(); }));
    auto* native = nativeWindow();
    auto* bridge = qobject_cast<gui::ChatBridge*>(QQmlEngine::contextForObject(native)->contextProperty("chat").value<QObject*>());
    REQUIRE(bridge); REQUIRE(bridge->hotSwitch());
    const auto firstState = state(native);
    CHECK(firstState.value("phase").toInt() == 6);
    CHECK(firstState.value("username").toString() == "hot_alice");
    CHECK(firstState.value("drafts").toMap().value("peer:hot_bob").toString() == "draft survives both views");
    CHECK(native->property("mode").toString() == "gate");
    REQUIRE(QTest::qWaitForWindowExposed(native,5000));
    REQUIRE(QMetaObject::invokeMethod(native, "enter", Q_ARG(QVariant, QVariant(false))));
    const bool openingAdvanced=waitFor([&] { return native->property("bootTime").toDouble() >= 4; }, 10000);
    if (!openingAdvanced) {
        std::cout << "Opening diagnostic: mode=" << native->property("mode").toString().toStdString()
            << " time=" << native->property("bootTime").toDouble() << " visible=" << native->isVisible() << " exposed=" << native->isExposed() << '\n';
        native->grabWindow().save(QString::fromStdWString((output / "opening-failure.png").wstring()));
    }
    REQUIRE(openingAdvanced);
    CHECK(native->property("mode").toString() == "boot");
    CHECK(state(native).value("phase").toInt() == 6);
    native->grabWindow().save(QString::fromStdWString((output / "native-boot-scan.png").wstring()));
    REQUIRE(waitFor([&] { return native->property("bootTime").toDouble() >= 23; }, 25000));
    CHECK(native->property("mode").toString() == "boot");
    native->grabWindow().save(QString::fromStdWString((output / "native-boot-array.png").wstring()));
    REQUIRE(waitFor([&] { return native->property("mode").toString() == "detail"; }, 18000));
    QTest::qWait(6000);
    auto* glass = native->findChild<QQuickItem*>("glassWorkspace"); REQUIRE(glass);
    CHECK(glass->isVisible()); CHECK(glass->width() >= 600); CHECK(glass->width() <= 1120);
    CHECK(glass->height() >= 490); CHECK(glass->scale() == 1);
    CHECK(qAbs(glass->x() + glass->width() - (native->width() - 24)) < 1);
    native->grabWindow().save(QString::fromStdWString((output / "native-online.png").wstring()));
    QList<QQuickItem*> rows;
    std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->objectName() == "messageRow") rows.append(item);
        for (auto* child : item->childItems()) visit(child);
    };
    visit(native->contentItem());
    REQUIRE(rows.size() >= 3);
    for (auto* row : rows) {
        CHECK(row->height() > 65);
        for (auto* other : rows) if (row != other && row->parentItem() == other->parentItem() && other->y() > row->y())
            CHECK(other->y() >= row->y() + row->height() - 0.5);
    }
    native->grabWindow().save(QString::fromStdWString((output / "native-online.png").wstring()));
    auto* preferences = native->findChild<QObject*>("rhinePreferences"); REQUIRE(preferences);
    preferences->setProperty("dark", true); QTest::qWait(2500);
    native->grabWindow().save(QString::fromStdWString((output / "native-dark-chat.png").wstring()));
    REQUIRE(QMetaObject::invokeMethod(native, "openIndex", Q_ARG(QVariant, QVariant(12))));
    REQUIRE(waitFor([&] { return native->property("navigationPhase").toString().isEmpty()
        && native->findChild<QQuickItem*>("glassChatPanel")->property("module").toString() == "files"; }, 12000));
    CHECK(native->findChild<QQuickItem*>("glassChatPanel")->property("module").toString() == "files");
    native->grabWindow().save(QString::fromStdWString((output / "native-dark-files.png").wstring()));
    // Sending without re-authenticating proves the native adapter uses the
    // live business session rather than just restoring an Online label.
    CHECK(bridge->command({{"command", "sendText"}, {"peer", "hot_bob"}, {"text", "from-native-live-session"}}).isEmpty());
    REQUIRE(waitFor([&] { return bp.hasMessage("from-native-live-session"); }));
    REQUIRE(bridge->switchFrontend("codex").isEmpty());
    REQUIRE(alice.isVisible()); CHECK(ap.online());
    CHECK(draft->toPlainText() == "draft survives both views");
    alice.grab().save(QString::fromStdWString((output / "codex-online.png").wstring()));
    const auto source = output / "payload.bin";
    const auto bytes = cc::crypto::randomBytes(4 * 1024 * 1024 + 15);
    { std::ofstream file(source, std::ios::binary); file.write(bytes.data(), bytes.size()); }
    ap.selectFile(QString::fromStdWString(source.wstring())); ap.sendSelectedFile();
    // Switch while the real file command is in flight; no transfer gate and
    // no disconnect/relogin should interrupt the queued work.
    switchButton->click(); QTest::qWait(50);
    CHECK(nativeWindow() == native); CHECK(state(native).value("phase").toInt() == 6);
    REQUIRE(bridge->switchFrontend("codex").isEmpty());
    REQUIRE(waitFor([&] { auto file = bp.fileCard(); return file && file->complete; }, 25000));
    CHECK(ap.online()); CHECK(bp.online());
    // File path naming is verified through the canonical downloaded file tree.
    bool matched = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(second.dataDirectory)) {
        if (!entry.is_regular_file() || entry.file_size() != bytes.size()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::string copy{std::istreambuf_iterator<char>(file), {}};
        if (copy == bytes) matched = true;
    }
    CHECK(matched);
    ap.sendMessage("after-hot-switch");
    REQUIRE(waitFor([&] { return bp.hasMessage("after-hot-switch") && ap.hasReceipt("after-hot-switch"); }));
    alice.close(); bob.close();
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    QSurfaceFormat::setDefaultFormat(QQuick3D::idealSurfaceFormat(4));
    QApplication app(argc, argv); return doctest::Context(argc, argv).run();
}
