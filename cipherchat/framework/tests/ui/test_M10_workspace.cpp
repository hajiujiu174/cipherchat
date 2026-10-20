#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "qt_client_page.hpp"
#include "feature_ui.hpp"
#include "rhine_native/chat_bridge.hpp"
#include "rhine_native/archive_field.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QSplitter>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <QJSValue>
#include <QTest>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtQuick3D/qquick3d.h>
#include <iostream>
using namespace ccfw;
namespace {
bool wait(const std::function<bool()>& p, int timeout = 10000) { QElapsedTimer e; e.start(); while (e.elapsed() < timeout) { if (p()) return true; QTest::qWait(20); } return p(); }
QQuickWindow* nativeWindow() { for (auto* w : QGuiApplication::allWindows()) if (w->objectName() == "rhineHotWindow") return qobject_cast<QQuickWindow*>(w); return nullptr; }
template<class T> T* widget(QWidget& w, const char* name) { auto* found = w.findChild<T*>(name); REQUIRE(found); return found; }
}
TEST_CASE("M10 two frontends preserve drafts and accessible logical-size workspaces") {
    QStringList arguments{"--baseline-probe"};
    const auto module = qEnvironmentVariable("CCFW_TEST_MODULE");
    if (!module.isEmpty()) { arguments.prepend(module); arguments.prepend("--module"); }
    QProcess baseline; baseline.start(QCoreApplication::applicationFilePath(), arguments);
    REQUIRE(baseline.waitForFinished(30000)); REQUIRE(baseline.exitCode() == 0);
    const auto record = QJsonDocument::fromJson(baseline.readAllStandardOutput().trimmed()).object();
    const auto baselineGap = record.value("maxGapMs").toInt(-1); REQUIRE(baselineGap > 0);
    const auto heartbeatLimit = std::max(250,baselineGap*2);
    const auto output = test::tempDirectory("M10-workspace"); test::RealServer server;
    gui::WindowOptions options; options.connection = server.options(); options.dataDirectory = output / "client"; options.username = "workspace_alice";
    gui::MainWindow window(options); window.show(); test::QtClientPage page(window); page.authenticate("password123", true); REQUIRE(wait([&] { return page.online(); }));
    // Online must only become visible after the authenticated restore view.
    page.openOwnSecurity(); CHECK(!page.ownFingerprint().isEmpty()); page.closeOwnSecurity();
    auto* peer = widget<QLineEdit>(window, "chatPeer"); auto* input = widget<QPlainTextEdit>(window, "chatInput");
    page.selectConversation("workspace_bob"); input->setPlainText("第一份草稿\n第二行");
    peer->setText("workspace_other"); input->setPlainText("另一会话草稿");
    peer->setText("workspace_bob"); CHECK(input->toPlainText() == QStringLiteral("第一份草稿\n第二行"));
    CHECK(window.findChild<QObject*>("pinConversation") == nullptr);
    CHECK(window.findChild<QObject*>("pinnedConversations") == nullptr);
    CHECK(window.findChild<QObject*>("workspaceFocus") == nullptr);
    QElapsedTimer clock; clock.start(); qint64 previous = 0, maxGap = 0; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] { const auto now = clock.elapsed(); if (previous) maxGap = std::max(maxGap, now - previous); previous = now; }); heartbeat.start(20);
    for (const auto& size : {QSize(960, 640), QSize(1366, 768), QSize(1920, 1080)}) for (bool dark : {false, true}) {
        window.resize(size); auto* appearance = widget<QPushButton>(window, "appearanceMode"); if (appearance->isChecked() != dark) appearance->click(); QTest::qWait(300);
        const auto* send = widget<QPushButton>(window, "sendMessage"); CHECK(send->isVisible()); CHECK(input->isVisible());
        CHECK(window.rect().contains(QRect(send->mapTo(&window, QPoint()), send->size()))); CHECK(input->width() >= 200);
        REQUIRE(window.grab().save(QString::fromStdWString((output / ("codex-" + std::to_string(size.width()) + (dark ? "-dark.png" : "-light.png"))).wstring())));
    }
    const auto nativeError = window.switchFrontend("rhine"); INFO(nativeError.toStdString());
    REQUIRE(nativeError.isEmpty()); REQUIRE(wait([&] { return nativeWindow(); })); auto* native = nativeWindow();
    native->raise(); native->requestActivate(); REQUIRE(QTest::qWaitForWindowActive(native, 5000));
    REQUIRE(QMetaObject::invokeMethod(native, "finishBoot", Q_ARG(QVariant, QVariant("archive"))));
    REQUIRE(QMetaObject::invokeMethod(native, "openIndex", Q_ARG(QVariant, QVariant(3))));
    REQUIRE(wait([&] { return native->property("mode").toString() == "detail" && native->property("navigationPhase").toString().isEmpty(); }));
    auto* glass = native->findChild<QQuickItem*>("glassWorkspace"); REQUIRE(glass); auto* panel = native->findChild<QQuickItem*>("glassChatPanel"); REQUIRE(panel);
    auto* preferences = native->findChild<QObject*>("rhinePreferences"); REQUIRE(preferences);
    CHECK(native->findChild<QObject*>("workspaceFocus") == nullptr);
    CHECK(native->findChild<QObject*>("pinCurrentPeer") == nullptr);
    CHECK(!native->property("workspaceFocus").isValid());
    const auto requestedScale=qEnvironmentVariable("QT_SCALE_FACTOR").toDouble();
    CHECK(qAbs(native->devicePixelRatio()-requestedScale)<0.01);
    std::cout<<"Native device pixel ratio="<<native->devicePixelRatio()<<'\n';
    for (const auto& size : {QSize(860, 640), QSize(960, 640), QSize(1366, 768), QSize(1920, 1080)}) for (bool dark : {false, true}) {
        native->setMinimumSize(size); native->resize(size); native->setPosition(0, 0); preferences->setProperty("dark", dark); QTest::qWait(800);
        CHECK(native->size() == size);
        CHECK(glass->isVisible()); CHECK(glass->scale() == 1); CHECK(glass->width() <= native->width()); CHECK(glass->height() <= native->height());
        CHECK(glass->width() >= 600); CHECK(glass->width() <= 1120);
        CHECK(qAbs(glass->x() + glass->width() - (native->width() - 24)) < 1);
        CHECK(panel->height() >= 230); CHECK(panel->width() >= 550);
        auto* scene = native->findChild<QQuickItem*>("rhineScene"); REQUIRE(scene);
        for (double x : {-2.7, 2.7}) for (double y : {-0.1, 3.9}) {
            QVariant point;
            REQUIRE(QMetaObject::invokeMethod(scene, "projectCard", Q_RETURN_ARG(QVariant, point), Q_ARG(QVariant, QVariant(x)), Q_ARG(QVariant, QVariant(y))));
            const auto projected = point.value<QVector3D>();
            INFO("Model projection " << projected.x() << "," << projected.y() << " workspace=" << glass->x());
            CHECK(projected.x() >= 8); CHECK(projected.x() <= glass->x() - 12);
            CHECK(projected.y() >= native->property("modelAreaTop").toDouble() - 10);
            CHECK(projected.y() <= native->height() - 155);
        }
        REQUIRE(native->grabWindow().save(QString::fromStdWString((output / ("rhine-" + std::to_string(size.width()) + (dark ? "-dark.png" : "-light.png"))).wstring())));
    }
    native->setMinimumSize(QSize(860,640));
    REQUIRE(window.switchFrontend("codex").isEmpty()); CHECK(page.online()); CHECK(input->toPlainText() == QStringLiteral("第一份草稿\n第二行")); CHECK(window.findChild<QObject*>("pinnedConversations") == nullptr);
    page.openOwnSecurity(); auto* card = widget<QDialog>(window, "ownIdentityDialog");
    REQUIRE(card->findChild<QLabel*>("identityUid")); CHECK(card->findChild<QLabel*>("identityUid")->text().toLongLong() > 0);
    CHECK(card->findChild<QLabel*>("identityQr") == nullptr); CHECK(card->findChild<QPlainTextEdit*>("identityCardInput") == nullptr);
    card->grab().save(QString::fromStdWString((output / "uid-card.png").wstring()));
    { std::ofstream fingerprint(output/"identity-fingerprint.txt"); fingerprint << card->findChild<QLabel*>("ownFingerprint")->text().toStdString(); }
    card->close();
    CHECK(maxGap <= heartbeatLimit); std::cout << "M10 UI scale=" << qEnvironmentVariable("QT_SCALE_FACTOR").toStdString() << " baseline cold-scene gap=" << baselineGap << "ms max heartbeat gap=" << maxGap << "ms limit=" << heartbeatLimit << "ms evidence=" << output.string() << '\n'; window.close();
}
TEST_CASE("M10 repeated identity events keep the visible card and comparison input stable") {
    QWidget owner; owner.show();
    gui::FeatureUi ui(&owner, [](gui::Command) { return true; }); ui.scope={"identity-ui-scope","alice"};
    PeerIdentity peer{"bob",cc::crypto::generateEd25519().pub,cc::crypto::generateX25519().pub,false};
    auto* card=ui.identityCard(peer); QTest::qWait(20);
    auto* input=card->findChild<QLineEdit*>("compareFingerprint"); REQUIRE(input); input->setText("comparison in progress");
    for (int i=0;i<20;++i) {
        ui.consume(gui::WorkerEvent{ClientEvent{peer}});
        CHECK(ui.identityCard(peer)==card); CHECK(input->text()=="comparison in progress");
    }
    peer.manuallyVerified=true; ui.consume(gui::WorkerEvent{ClientEvent{peer}});
    CHECK(card->isVisible()); CHECK(!card->findChild<QPushButton*>("confirmIdentity")->isEnabled());
    CHECK(card->findChild<QLabel*>("identityCompareResult")->text()==QStringLiteral("已人工核对"));
    peer.dhPublicKey=cc::crypto::generateX25519().pub; peer.manuallyVerified=false;
    ui.consume(gui::WorkerEvent{ClientEvent{peer}}); CHECK(!card->findChild<QPushButton*>("confirmIdentity")->isEnabled());
    CHECK(card->findChild<QLabel*>("identityCompareResult")->text().contains(QStringLiteral("已变化")));
    auto* updated=ui.identityCard(peer); CHECK(updated!=card); CHECK_FALSE(updated->findChild<QPushButton*>("confirmIdentity")->isEnabled()); QTest::mouseClick(updated->findChild<QPushButton*>("peerSecurityDetails"),Qt::LeftButton); CHECK(updated->findChild<QPushButton*>("confirmIdentity")->isEnabled());
    updated->close(); QTest::qWait(20); owner.close();
}
int main(int argc, char** argv) {
    QSurfaceFormat::setDefaultFormat(QQuick3D::idealSurfaceFormat(4)); QApplication app(argc, argv);
    if (app.arguments().contains("--baseline-probe")) {
        Q_INIT_RESOURCE(rhine_native);
        qmlRegisterType<gui::ArchiveField>("Rhine", 1, 0, "ArchiveField");
        gui::ChatBridge bridge({{"dataDirectory", QString::fromStdWString(test::tempDirectory("M10-ui-baseline").wstring())}});
        QQmlApplicationEngine engine; auto* context = engine.rootContext();
        context->setContextProperty("chat", &bridge); context->setContextProperty("embeddedHotSwitch", true);
        context->setContextProperty("uiFont", "Microsoft YaHei UI"); context->setContextProperty("boldFont", "Microsoft YaHei UI");
        context->setContextProperty("skipOpening", true); context->setContextProperty("captureDir", "");
        context->setContextProperty("tune", QVariantMap{}); context->setContextProperty("freezeAt", -1.0);
        QElapsedTimer elapsed; elapsed.start(); qint64 previous = 0, gap = 0; QTimer heartbeat;
        QObject::connect(&heartbeat,&QTimer::timeout,&app,[&] { const auto now = elapsed.elapsed(); if (previous) gap=std::max(gap,now-previous); previous=now; });
        heartbeat.start(20); QTest::qWait(60);
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(CCFW_M10_BASELINE_QML)));
        if (engine.rootObjects().isEmpty()) return 2;
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); if (!window) return 3;
        window->show(); QTest::qWait(900); window->hide();
        std::cout << QJsonDocument(QJsonObject{{"maxGapMs",int(gap)},{"dpr",window->devicePixelRatio()}}).toJson(QJsonDocument::Compact).toStdString(); return 0;
    }
    return doctest::Context(argc, argv).run();
}
