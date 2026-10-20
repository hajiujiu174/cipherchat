#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "qt_client_page.hpp"
#include "rhine_native/chat_bridge.hpp"
#include "rhine_native/archive_field.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QPushButton>
#include <QLabel>
#include <QJsonDocument>
#include <QFile>
#include <QClipboard>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QTest>
#include <QWheelEvent>
#include <QtQuick3D/qquick3d.h>
#include <fstream>
#include <iostream>
using namespace ccfw;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 12000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) { if (predicate()) return true; QTest::qWait(20); }
    return predicate();
}
QVariant unpack(const QVariant& value) { return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant() : value; }
QQuickWindow* nativeWindow() {
    for (auto* w : QGuiApplication::allWindows()) if (w->objectName() == "rhineHotWindow") return qobject_cast<QQuickWindow*>(w);
    return nullptr;
}
QQuickItem* visual(QQuickItem* root, const QString& name) {
    if (!root->isVisible() || root->opacity() <= 0.01) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* found = visual(child, name)) return found;
    return nullptr;
}
class RhinePage {
public:
    QQuickWindow* window;
    explicit RhinePage(QQuickWindow* w) : window(w) {}
    QQuickItem* item(const QString& name) {
        auto* found = visual(window->contentItem(), name); INFO("Requested item: " << name.toStdString()); REQUIRE(found); return found;
    }
    void click(const QString& name) {
        auto* modal=window->findChild<QQuickItem*>("terminalModal");
        if(modal && window->property("modal").toString().isEmpty() && modal->isVisible())
            REQUIRE(waitFor([&] { return !modal->isVisible(); }));
        auto* target = item(name); const auto point = target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint();
        INFO(name.toStdString()); REQUIRE(point.x() >= 0); REQUIRE(point.x() < window->width());
        REQUIRE(point.y() >= 0); REQUIRE(point.y() < window->height());
        window->requestActivate(); REQUIRE(QTest::qWaitForWindowActive(window, 5000));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point); QTest::qWait(40);
        if (name=="openSettings") REQUIRE(waitFor([&] { return window->findChild<QQuickItem*>("terminalModal")->opacity()>0.999; }));
        if (name=="closeTerminalModal") REQUIRE(waitFor([&] { return !window->findChild<QQuickItem*>("terminalModal")->isVisible(); }));
    }
    void input(const QString& name, const QString& text) {
        click(name); QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(window, Qt::Key_Backspace);
        auto* focus=window->activeFocusItem(); bool inside=false;
        for(auto* current=focus;current;current=current->parentItem()) if(current==item(name)) inside=true;
        INFO("Input: " << name.toStdString() << " focus=" << (focus ? focus->objectName().toStdString() : "none") << " dpr=" << window->devicePixelRatio());
        if (!inside) window->grabWindow().save(QCoreApplication::applicationDirPath()+"/input-focus-failure.png");
        REQUIRE(inside);
        QInputMethodEvent commit; commit.setCommitString(text);
        REQUIRE(window->activeFocusItem()); QCoreApplication::sendEvent(window->activeFocusItem(), &commit); QTest::qWait(30);
    }
    void module(const QString& name) {
        click("navigate-" + name);
        REQUIRE(waitFor([&] { auto* panel = visual(window->contentItem(), "glassChatPanel");
            return panel && panel->property("module").toString() == name && window->property("navigationPhase").toString().isEmpty(); }));
    }
    QVariantMap reading() {
        QVariant value; REQUIRE(QMetaObject::invokeMethod(item("conversationView"), "reading", Q_RETURN_ARG(QVariant, value)));
        return unpack(value).toMap();
    }
    void wheel(int delta) {
        auto* target = item("messageTimeline"); const auto point = target->mapToScene(QPointF(target->width()/2,target->height()/2));
        QWheelEvent event(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0,delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &event); QTest::qWait(250);
    }
};
}
TEST_CASE("Rhine guest login is compact and only authenticated sessions expose business navigation") {
    const auto output = test::tempDirectory("Rhine-login-layout"); test::RealServer server;
    gui::WindowOptions options; options.connection = server.options(); options.dataDirectory = output / "client";
    gui::MainWindow owner(options); owner.show();
    REQUIRE(owner.switchFrontend("rhine").isEmpty()); REQUIRE(waitFor([] { return nativeWindow(); }));
    auto* native = nativeWindow(); native->resize(1600, 900); native->show();
    REQUIRE(QTest::qWaitForWindowExposed(native, 5000)); native->requestActivate(); REQUIRE(QTest::qWaitForWindowActive(native, 5000));
    REQUIRE(QMetaObject::invokeMethod(native, "finishBoot", Q_ARG(QVariant, QVariant("archive"))));
    REQUIRE(waitFor([&] { return native->property("mode").toString() == "detail" && native->property("navigationPhase").toString().isEmpty(); }));
    RhinePage page(native); auto* store = native->findChild<QObject*>("chatStore"); REQUIRE(store);
    auto* preferences = native->findChild<QObject*>("rhinePreferences"); REQUIRE(preferences);
    auto* glass = page.item("glassWorkspace");
    CHECK(glass->property("locked").toBool()); CHECK(!store->property("sessionAvailable").toBool());
    for (const auto& name : {"navigate-chat", "navigate-contacts", "navigate-groups", "navigate-files", "openIndexSearch", "openPinned", "workspaceFocus", "messageEditor"})
        CHECK_MESSAGE(!visual(native->contentItem(), name), name);
    CHECK(visual(native->contentItem(), "navigate-account")); CHECK(visual(native->contentItem(), "openSettings"));
    CHECK(!visual(native->contentItem(), "workspaceTab-overview"));
    page.click("openSettings"); CHECK(native->property("modal").toString() == "settings");
    QTest::keyClick(native, Qt::Key_Escape); CHECK(native->property("modal").toString().isEmpty());
    QTest::keyClick(native, Qt::Key_1, Qt::ControlModifier); CHECK(!visual(native->contentItem(), "messageEditor"));
    REQUIRE(QMetaObject::invokeMethod(native, "navigateModule", Q_ARG(QVariant, QVariant("files"))));
    CHECK(native->property("selected").toInt() == 0);
    for (const auto& size : {QSize(860, 640), QSize(960, 640), QSize(1366, 768), QSize(1920, 1080)}) for (bool dark : {false, true}) {
        native->setMinimumSize(size); native->resize(size); native->setPosition(0, 0); preferences->setProperty("dark", dark); QTest::qWait(1000);
        CHECK(native->size() == size);
        auto* terminal = glass->property("terminal").value<QObject*>();
        std::cout << "Guest geometry native=" << native->width() << 'x' << native->height()
                  << " content=" << native->contentItem()->width() << 'x' << native->contentItem()->height()
                  << " bound=" << (terminal == native) << " root=" << (terminal ? terminal->property("width").toInt() : 0)
                  << " glass=" << glass->x() << " active=" << native->isActive() << '\n';
        CHECK(glass->width() == 440); CHECK(glass->height() <= 470);
        auto* scene = page.item("rhineScene");
        for (double x : {-2.7, 2.7}) for (double y : {-0.1, 3.9}) {
            QVariant point;
            REQUIRE(QMetaObject::invokeMethod(scene, "projectCard", Q_RETURN_ARG(QVariant, point), Q_ARG(QVariant, QVariant(x)), Q_ARG(QVariant, QVariant(y))));
            const auto projected = point.value<QVector3D>();
            INFO("Guest model projection " << projected.x() << "," << projected.y() << " workspace=" << glass->x());
            CHECK(projected.x() >= 8); CHECK(projected.x() <= glass->x() - 12);
            CHECK(projected.y() >= native->property("modelAreaTop").toDouble() - 10); CHECK(projected.y() <= native->height() - 155);
        }
        auto* login = page.item("signIn"); const auto loginPoint = login->mapToScene(QPointF());
        CHECK(QRectF(0, 0, native->width(), native->height()).contains(QRectF(loginPoint, QSizeF(login->width(), login->height()))));
        REQUIRE(native->grabWindow().save(QString::fromStdWString((output / ("guest-" + std::to_string(size.width()) + (dark ? "-dark.png" : "-light.png"))).wstring())));
    }
    native->setMinimumSize(QSize(860, 640)); native->resize(1366, 768); preferences->setProperty("dark", false); QTest::qWait(300);
    page.input("accountUsername", "layout_alice"); page.input("accountPassword", "password123"); page.click("signIn");
    REQUIRE(waitFor([&] { return !store->property("lastError").toString().isEmpty() && store->property("phase").toInt() != 5; }));
    CHECK(glass->property("locked").toBool()); CHECK(!visual(native->contentItem(), "navigate-chat"));
    CHECK(!page.item("signInFeedback")->property("text").toString().isEmpty());
    page.input("accountPassword", "password123"); page.click("registerAccount");
    REQUIRE(waitFor([&] { auto* body = visual(native->contentItem(), "glassChatPanel");
        return store->property("phase").toInt() == 6 && body && body->property("module").toString() == "chat"
            && native->property("mode").toString() == "detail" && native->property("navigationPhase").toString().isEmpty(); }));
    CHECK(!glass->property("locked").toBool()); CHECK(glass->width() >= 600);
    for (const auto& name : {"navigate-chat", "navigate-contacts", "navigate-groups", "navigate-files", "navigate-account"}) CHECK(visual(native->contentItem(), name));
    REQUIRE(QMetaObject::invokeMethod(store, "choosePeer", Q_ARG(QVariant, QVariant("saved_peer"))));
    REQUIRE(waitFor([&] { return visual(native->contentItem(), "messageEditor"); }));
    page.input("messageEditor", "keep this draft while disconnected");
    auto* bridge = qobject_cast<gui::ChatBridge*>(QQmlEngine::contextForObject(native)->contextProperty("chat").value<QObject*>()); REQUIRE(bridge);
    CHECK(bridge->command({{"command", "disconnect"}}).isEmpty());
    REQUIRE(waitFor([&] { return store->property("phase").toInt() != 6; }));
    CHECK(store->property("sessionAvailable").toBool()); CHECK(visual(native->contentItem(), "navigate-chat"));
    QTest::keyClick(native, Qt::Key_Return); CHECK(page.item("messageEditor")->property("text").toString() == "keep this draft while disconnected");
    CHECK(!page.item("conversationView")->property("errorText").toString().isEmpty());
    std::cout << "Rhine guest layout evidence=" << output.string() << '\n'; owner.close();
}
TEST_CASE("Rhine daily controls preserve real TLS drafts history IME and recipient context") {
    const auto output = test::tempDirectory("Rhine-usability"); test::RealServer server;
    gui::WindowOptions options; options.connection = server.options(); options.dataDirectory = output / "alice"; options.username = "usability_alice";
    auto second = options; second.dataDirectory = output / "bob"; second.username = "usability_bob";
    gui::MainWindow alice(options), bob(second); alice.show(); bob.show();
    test::QtClientPage ap(alice), bp(bob); ap.authenticate("password123", true); bp.authenticate("password123", true);
    REQUIRE(waitFor([&] { return ap.online() && bp.online(); }));
    ap.selectConversation("usability_bob"); bp.selectConversation("usability_alice");
    alice.findChild<QPushButton*>("inspectIdentity")->click(); bob.findChild<QPushButton*>("inspectIdentity")->click();
    REQUIRE(waitFor([&] { return ap.identityConfirmationPending() && bp.identityConfirmationPending(); }));
    ap.openPeerSecurity(); bp.openPeerSecurity(); ap.confirmIdentity(); bp.confirmIdentity();
    REQUIRE(waitFor([&] { return !ap.identityConfirmationPending() && !bp.identityConfirmationPending(); }));
    for (int i=0;i<112;i++) {
        const auto text=QString("archive-%1 · retained history").arg(i,3,10,QChar('0'));
        ap.sendMessage(text);
        REQUIRE(waitFor([&] { return bp.hasMessage(text) && ap.hasReceipt(text); }));
    }
    REQUIRE(waitFor([&] { return bp.hasMessage("archive-111 · retained history") && ap.hasReceipt("archive-111 · retained history"); }, 20000));
    REQUIRE(alice.switchFrontend("rhine").isEmpty()); REQUIRE(waitFor([] { return nativeWindow(); }));
    auto* native = nativeWindow(); native->resize(1600,900); native->show();
    REQUIRE(QTest::qWaitForWindowExposed(native,5000));
    native->raise(); native->requestActivate(); REQUIRE(QTest::qWaitForWindowActive(native,5000));
    REQUIRE(QMetaObject::invokeMethod(native,"finishBoot",Q_ARG(QVariant,QVariant("archive"))));
    REQUIRE(waitFor([&] { return native->property("mode").toString()=="detail" && native->property("navigationPhase").toString().isEmpty(); }));
    auto* store = native->findChild<QObject*>("chatStore"); REQUIRE(store);
    auto* bridge = qobject_cast<gui::ChatBridge*>(QQmlEngine::contextForObject(native)->contextProperty("chat").value<QObject*>()); REQUIRE(bridge);
    auto warnings=std::make_shared<QStringList>();
    QObject::connect(QQmlEngine::contextForObject(native)->engine(), &QQmlEngine::warnings, native,
        [warnings](const QList<QQmlError>& values) { for (const auto& value : values) { warnings->append(value.toString()); std::cerr << value.toString().toStdString() << '\n'; } });
    RhinePage page(native);
    REQUIRE(page.item("messageTimeline")->property("count").toInt()==100);
    page.wheel(24000);
    REQUIRE(waitFor([&] { return !page.item("messageTimeline")->property("moving").toBool() && !page.item("conversationView")->property("adjusting").toBool(); }));
    CHECK(!page.item("conversationView")->property("following").toBool());
    const auto beforeIncoming=page.reading();
    bp.sendMessage("incoming before loading earlier history");
    REQUIRE(waitFor([&] { return page.item("conversationView")->property("totalMessages").toInt()==113 && !page.item("conversationView")->property("adjusting").toBool(); }));
    CHECK(page.reading().value("anchor")==beforeIncoming.value("anchor"));
    CHECK(page.item("messageTimeline")->property("count").toInt()==101);
    const auto beforeLoad=page.reading(); page.click("loadEarlier");
    std::cout<<"History after load: visible="<<page.item("messageTimeline")->property("count").toInt()
        <<" total="<<page.item("conversationView")->property("totalMessages").toInt()
        <<" limit="<<page.item("conversationView")->property("messageLimit").toInt()<<'\n';
    REQUIRE(waitFor([&] { return page.item("messageTimeline")->property("count").toInt()==113 && !page.item("conversationView")->property("adjusting").toBool(); }));
    const auto afterLoad=page.reading(); CHECK(afterLoad.value("anchor")==beforeLoad.value("anchor"));
    CHECK(qAbs(afterLoad.value("offset").toDouble()-beforeLoad.value("offset").toDouble())<3);
    bp.sendMessage("incoming while reading old messages");
    REQUIRE(waitFor([&] { return page.item("conversationView")->property("totalMessages").toInt()==114 && !page.item("conversationView")->property("adjusting").toBool(); }));
    CHECK(page.reading().value("anchor")==afterLoad.value("anchor"));
    CHECK(!page.item("conversationView")->property("following").toBool());
    page.input("messageEditor","draft for bob / with two lines\nsecond line");
    page.click("newConversation"); page.input("newPeerName","temporary_peer"); page.click("openNewPeer");
    REQUIRE(waitFor([&] { return store->property("selectedPeer").toString()=="temporary_peer"; }));
    page.input("messageEditor","another draft"); page.click("session-usability_bob");
    CHECK(page.item("messageEditor")->property("text").toString()=="draft for bob / with two lines\nsecond line");
    CHECK(page.reading().value("anchor")==afterLoad.value("anchor"));
    CHECK(native->property("navigationPhase").toString().isEmpty());
    page.input("messageEditor","/cursor"); QTest::keyClick(native,Qt::Key_Left); CHECK(native->property("modal").toString().isEmpty());
    page.input("messageEditor","");
    QInputMethodEvent preedit(QStringLiteral("中文候选"),{}); QCoreApplication::sendEvent(native->activeFocusItem(),&preedit);
    CHECK(page.item("messageEditor")->property("inputMethodComposing").toBool());
    const auto composingCount=unpack(store->property("history")).toList().size();
    QTest::keyClick(native,Qt::Key_Return); QTest::qWait(150);
    CHECK(unpack(store->property("history")).toList().size()==composingCount);
    QInputMethodEvent commit; commit.setCommitString(QStringLiteral("中文输入验证"));
    QCoreApplication::sendEvent(native->activeFocusItem(),&commit);
    CHECK(page.item("messageEditor")->property("text").toString()==QStringLiteral("中文输入验证"));
    CHECK(!page.item("messageEditor")->property("inputMethodComposing").toBool());
    QTest::keyClick(native,Qt::Key_Return);
    const bool chineseReceived=waitFor([&] { return bp.hasMessage(QStringLiteral("中文输入验证")); });
    INFO("Editor after Return: "<<page.item("messageEditor")->property("text").toString().toStdString());
    INFO("Selected peer: "<<store->property("selectedPeer").toString().toStdString());
    INFO("Error: "<<page.item("conversationView")->property("errorText").toString().toStdString());
    REQUIRE(chineseReceived);
    REQUIRE(waitFor([&] { for (const auto& value:unpack(store->property("history")).toList()) {
        const auto entry=value.toMap(); if(entry.value("text").toString()==QStringLiteral("中文输入验证") && entry.value("state").toInt()==2) return true;
    } return false; }));
    page.input("messageEditor","first line"); QTest::keyClick(native,Qt::Key_Return,Qt::ShiftModifier);
    QInputMethodEvent secondLine; secondLine.setCommitString("second line"); QCoreApplication::sendEvent(native->activeFocusItem(),&secondLine);
    QTest::keyClick(native,Qt::Key_Return,Qt::ControlModifier);
    REQUIRE(waitFor([&] { return bp.hasMessage("first line\nsecond line"); }));
    page.click("openSettings"); page.click("enterSendPreference"); page.click("closeTerminalModal");
    page.input("messageEditor","legacy send");
    QTest::keyClick(native,Qt::Key_Return); CHECK(page.item("messageEditor")->property("text").toString()=="legacy send\n");
    QTest::keyClick(native,Qt::Key_Return,Qt::ControlModifier); REQUIRE(waitFor([&] { return bp.hasMessage("legacy send\n"); }));
    page.click("openSettings"); page.click("enterSendPreference"); QTest::keyClick(native,Qt::Key_Escape);
    CHECK(native->property("modal").toString().isEmpty());
    const auto payload = output / std::filesystem::path(u8"long filename with spaces and 中文.bin");
    const auto bytes=cc::crypto::randomBytes(1024*1024+31);
    { std::ofstream f(payload,std::ios::binary); f.write(bytes.data(),bytes.size()); }
    page.click("chooseAttachment");
    auto* picker=native->findChild<QObject*>("attachmentFileDialog"); REQUIRE(picker);
    CHECK(picker->property("recipient").toString()=="usability_bob");
    REQUIRE(QMetaObject::invokeMethod(store,"choosePeer",Q_ARG(QVariant,QVariant("temporary_peer"))));
    REQUIRE(picker->setProperty("selectedFile",QUrl::fromLocalFile(QString::fromStdWString(payload.wstring()))));
    REQUIRE(QMetaObject::invokeMethod(picker,"accepted")); REQUIRE(QMetaObject::invokeMethod(picker,"close"));
    QTest::qWait(300);
    CHECK(unpack(store->property("pendingAttachment")).toMap().value("peer").toString()=="usability_bob");
    const auto pickedPath=unpack(store->property("pendingAttachment")).toMap().value("path").toString();
    INFO("Selected local file: "<<pickedPath.toStdString());
    REQUIRE(QFile::exists(pickedPath));
    page.click("sendPendingAttachment");
    REQUIRE(waitFor([&]{return unpack(store->property("pendingAttachment")).toMap().value("path").toString().isEmpty()
        || !unpack(store->property("conversationErrors")).toMap().value("peer:usability_bob").toString().isEmpty();}));
    const bool fileReceived=waitFor([&] { auto file=bp.fileCard(); return file && file->complete; },25000);
    INFO("Last worker error: "<<store->property("lastError").toString().toStdString());
    INFO("File submission error: "<<unpack(store->property("conversationErrors")).toMap().value("peer:usability_bob").toString().toStdString());
    INFO("Sender files: "<<QJsonDocument::fromVariant(unpack(store->property("files"))).toJson(QJsonDocument::Compact).toStdString());
    REQUIRE(fileReceived);
    bool matched=false;
    for (const auto& entry:std::filesystem::recursive_directory_iterator(second.dataDirectory)) {
        if (!entry.is_regular_file() || entry.file_size()!=bytes.size()) continue;
        std::ifstream f(entry.path(),std::ios::binary); std::string copy{std::istreambuf_iterator<char>(f),{}};
        if (copy==bytes) matched=true;
    }
    CHECK(matched);
    page.click("session-temporary_peer");
    page.input("messageEditor","recover an asynchronously rejected draft");QTest::keyClick(native,Qt::Key_Return);
    REQUIRE(waitFor([&]{return !page.item("conversationView")->property("errorText").toString().isEmpty()
        && page.item("messageEditor")->property("text").toString()=="recover an asynchronously rejected draft";}));
    page.click("session-usability_bob");
    native->resize(860,640); QTest::qWait(300); page.click("openSessionDrawer");
    page.input("sessionSearch","usability_bob"); page.click("session-usability_bob");
    CHECK(!page.item("glassWorkspace")->property("drawerOpen").toBool());
    CHECK(native->findChild<QObject*>("workspaceFocus") == nullptr);
    CHECK(native->findChild<QObject*>("pinCurrentPeer") == nullptr);
    page.click("openSessionDrawer"); CHECK(page.item("glassWorkspace")->property("drawerOpen").toBool());
    QTest::keyClick(native,Qt::Key_Escape); CHECK(!page.item("glassWorkspace")->property("drawerOpen").toBool());
    page.click("openSettings"); REQUIRE(native->grabWindow().save(QString::fromStdWString((output/"settings-860.png").wstring())));
    page.click("closeTerminalModal"); native->resize(1600,900); QTest::qWait(200);
    page.click("navigate-contacts"); page.click("navigate-files"); page.click("navigate-chat");
    REQUIRE(waitFor([&] { return native->property("navigationPhase").toString().isEmpty() && page.item("glassChatPanel")->property("module").toString()=="chat"; }));
    page.input("messageEditor","final retained draft");
    REQUIRE(native->grabWindow().save(QString::fromStdWString((output/"chat-1600.png").wstring())));
    CHECK(bridge->command({{"command","disconnect"}}).isEmpty());
    REQUIRE(waitFor([&] { return store->property("phase").toInt()!=6; }));
    QTest::keyClick(native,Qt::Key_Return); CHECK(page.item("messageEditor")->property("text").toString()=="final retained draft");
    CHECK(!page.item("conversationView")->property("errorText").toString().isEmpty());
    for (const auto& warning:*warnings) CHECK_MESSAGE((!warning.contains("ReferenceError") && !warning.contains("TypeError") && !warning.contains("Binding loop")),warning.toStdString());
    std::cout<<"Rhine usability evidence="<<output.string()<<'\n'; alice.close(); bob.close();
}
TEST_CASE("Rhine contact discovery group creation and native account controls") {
    const auto output=test::tempDirectory("Rhine-contact-group"); test::RealServer server;
    gui::WindowOptions options; options.connection=server.options(); options.dataDirectory=output/"alice"; options.username="group_alice";
    auto second=options; second.dataDirectory=output/"bob"; second.username="group_bob";
    gui::MainWindow alice(options),bob(second); alice.show();bob.show(); test::QtClientPage ap(alice),bp(bob);
    ap.authenticate("password123",true); bp.authenticate("password123",true); REQUIRE(waitFor([&]{return ap.online()&&bp.online();}));
    ap.selectConversation("group_bob");bp.selectConversation("group_alice");
    alice.findChild<QPushButton*>("inspectIdentity")->click();bob.findChild<QPushButton*>("inspectIdentity")->click();
    REQUIRE(waitFor([&]{return ap.identityConfirmationPending()&&bp.identityConfirmationPending();}));
    ap.openPeerSecurity();bp.openPeerSecurity();ap.confirmIdentity();bp.confirmIdentity();QTest::qWait(150);
    REQUIRE(alice.switchFrontend("rhine").isEmpty()); REQUIRE(waitFor([]{return nativeWindow();}));
    auto* native=nativeWindow();native->resize(1600,900);native->show();REQUIRE(QTest::qWaitForWindowExposed(native,5000));
    native->raise(); native->requestActivate(); REQUIRE(QTest::qWaitForWindowActive(native,5000));
    REQUIRE(QMetaObject::invokeMethod(native,"finishBoot",Q_ARG(QVariant,QVariant("archive"))));
    REQUIRE(waitFor([&]{return native->property("mode").toString()=="detail"&&native->property("navigationPhase").toString().isEmpty();}));
    RhinePage page(native); auto* store=native->findChild<QObject*>("chatStore");REQUIRE(store);
    page.module("contacts");page.click("workspaceTab-notes");page.input("contactUserSearch","group_bob");page.click("searchUsers");
    REQUIRE(waitFor([&]{return visual(native->contentItem(),"contact-add-group_bob");}));page.click("contact-add-group_bob");
    auto accept=[&]() -> QPushButton* {for(auto* b:bob.findChildren<QPushButton*>("acceptFriend"))if(b->property("username").toString()=="group_alice")return b;return nullptr;};
    REQUIRE(waitFor([&]{return accept()!=nullptr;}));accept()->click();
    REQUIRE(waitFor([&]{return unpack(store->property("contacts")).toList().size()==1;}));
    page.click("workspaceTab-overview");page.click("contact-open-group_bob");
    REQUIRE(waitFor([&]{auto* panel=visual(native->contentItem(),"glassChatPanel");return panel&&panel->property("module").toString()=="chat"&&native->property("navigationPhase").toString().isEmpty();}));
    CHECK(store->property("selectedPeer").toString()=="group_bob");
    page.module("groups");page.click("workspaceTab-notes");page.input("groupName",QStringLiteral("日常操作测试群"));
    page.click("groupMember-group_bob");page.click("createGroup");
    REQUIRE(waitFor([&]{return unpack(store->property("groups")).toList().size()==1;}));
    const auto group=unpack(store->property("groups")).toList().first().toMap();
    CHECK(group.value("name").toString()==QStringLiteral("日常操作测试群"));CHECK(group.value("members").toStringList().contains("group_bob"));
    page.click("workspaceTab-overview");page.click("session-"+group.value("id").toString());
    CHECK(store->property("selectedGroup").toLongLong()==group.value("id").toLongLong());
    page.input("messageEditor","from native group");QTest::keyClick(native,Qt::Key_Return);bp.showGroups();
    REQUIRE(waitFor([&]{for(auto* label:bob.findChildren<QLabel*>())if(label->text()=="from native group")return true;return false;}));
    auto savedClipboard=std::make_unique<QMimeData>();
    if(const auto* original=QApplication::clipboard()->mimeData())
        for(const auto& format:original->formats()) savedClipboard->setData(format,original->data(format));
    page.click("selectableMessage");QTest::keyClick(native,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(native,Qt::Key_C,Qt::ControlModifier);
    CHECK((QApplication::clipboard()->text()=="from native group"));
    QApplication::clipboard()->setMimeData(savedClipboard.release());
    page.input("messageEditor","retained group draft");page.module("chat");page.module("groups");
    CHECK(page.item("messageEditor")->property("text").toString()=="retained group draft");
    REQUIRE(native->grabWindow().save(QString::fromStdWString((output/"group-1600.png").wstring())));
    auto* bridge=qobject_cast<gui::ChatBridge*>(QQmlEngine::contextForObject(native)->contextProperty("chat").value<QObject*>());REQUIRE(bridge);
    CHECK(bridge->command({{"command","disconnect"}}).isEmpty());REQUIRE(waitFor([&]{return store->property("phase").toInt()!=6;}));
    page.module("account");page.input("accountUsername","group_alice");page.input("accountPassword","password123");page.click("signIn");
    REQUIRE(waitFor([&]{auto* body=visual(native->contentItem(),"glassChatPanel");return store->property("phase").toInt()==6
        &&body&&body->property("module").toString()=="chat"&&native->property("mode").toString()=="detail"&&native->property("navigationPhase").toString().isEmpty();}));
    CHECK(store->property("username").toString()=="group_alice");
    CHECK(unpack(store->property("drafts")).toMap().value("group:"+group.value("id").toString()).toString()=="retained group draft");
    auto* field=native->findChild<gui::ArchiveField*>();REQUIRE(field);
    const auto rotation=field->modelRotation().y();page.click("rotateModel");
    REQUIRE(waitFor([&]{return qAbs(field->modelRotation().y()-rotation)>20;},5000));
    REQUIRE(waitFor([&]{return qAbs(field->modelRotation().y())<2;},8000));
    const auto beforeDrag=field->modelRotation().y();
    QVariant modelCenter;
    REQUIRE(QMetaObject::invokeMethod(page.item("rhineScene"),"projectCard",Q_RETURN_ARG(QVariant,modelCenter),Q_ARG(QVariant,QVariant(0.0)),Q_ARG(QVariant,QVariant(1.85))));
    const auto projectedCenter=modelCenter.value<QVector3D>();
    const QPoint dragStart(qRound(projectedCenter.x()),qRound(projectedCenter.y()));
    QTest::mousePress(native,Qt::LeftButton,Qt::NoModifier,dragStart);
    QTest::mouseMove(native,dragStart+QPoint(80,0),50);QTest::mouseRelease(native,Qt::LeftButton,Qt::NoModifier,dragStart+QPoint(80,0));
    REQUIRE(waitFor([&]{return qAbs(field->modelRotation().y()-beforeDrag)>3;},5000));
    page.click("returnToArray");REQUIRE(waitFor([&]{return field->canBrowse();}));
    QTest::mousePress(native,Qt::LeftButton,Qt::NoModifier,QPoint(200,450));
    QTest::mouseMove(native,QPoint(290,450),50);QTest::qWait(30);CHECK(field->dragging());
    QTest::mouseMove(native,QPoint(390,450),50);QTest::mouseRelease(native,Qt::LeftButton,Qt::NoModifier,QPoint(390,450));
    CHECK(!field->dragging());QTest::qWait(800);
    REQUIRE(native->grabWindow().save(QString::fromStdWString((output/"model-array-drag.png").wstring())));
    std::cout<<"Rhine contacts and group evidence="<<output.string()<<'\n';alice.close();bob.close();
}
int main(int argc,char** argv) {
    QSurfaceFormat::setDefaultFormat(QQuick3D::idealSurfaceFormat(4));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv); std::cout<<std::unitbuf; std::cerr<<std::unitbuf;
    return doctest::Context(argc,argv).run();
}
