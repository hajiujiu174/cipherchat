#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "qt_client_page.hpp"
#include "business_worker.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTimer>
#include <QTextCursor>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTest>
#include <fstream>
#include <iostream>
using namespace ccfw;
namespace {
template<class T> T* widget(QWidget& root, const char* name) { auto* result = root.findChild<T*>(name); REQUIRE(result); return result; }
bool wait(const std::function<bool()>& predicate, int timeout = 9000) { QElapsedTimer timer; timer.start(); while (timer.elapsed() < timeout) { if (predicate()) return true; QTest::qWait(20); } return predicate(); }
void screenshot(QWidget& window, const std::filesystem::path& path) { REQUIRE(window.grab().save(QString::fromStdWString(path.wstring()))); }
std::string bytes(const std::filesystem::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
void menu(QWidget& root, const char* entry, const QString& title) {
    auto* button = widget<QPushButton>(root, entry); REQUIRE(button->isVisible()); auto* popup = button->menu(); REQUIRE(popup);
    QAction* selected = nullptr; for (auto* action : popup->actions()) if (action->text() == title) selected = action; REQUIRE(selected);
    QTimer::singleShot(30, popup, [popup, selected] { QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, popup->actionGeometry(selected).center()); }); QTest::mouseClick(button, Qt::LeftButton);
}
}
TEST_CASE("QQ shell gates login and keeps real TLS messages groups drafts attachments and tray") {
    test::RealServer server; const auto output = test::tempDirectory("Codex-QQ-workspace");
    const auto profiles = std::filesystem::path(CCFW_QQ_TEST_ROOT) / ("p" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000));
    std::filesystem::create_directories(profiles); // Keep the full download path below MAX_PATH while exercising a long basename.
    gui::WindowOptions options; options.connection = server.options(); options.dataDirectory = profiles / "alice"; options.username = "qq_alice";
    auto second = options; second.dataDirectory = profiles / "bob"; second.username = "qq_bob";
    gui::MainWindow alice(options), bob(second); alice.show(); bob.show();
    REQUIRE(QTest::qWaitForWindowExposed(&alice)); REQUIRE(QTest::qWaitForWindowExposed(&bob));
    test::QtClientPage ap(alice), bp(bob);
    for (auto* name : {"messagesNav", "contactsNav", "filesNav", "accountMenu", "mainMenu"}) CHECK_FALSE(widget<QWidget>(alice, name)->isVisible());
    CHECK(widget<QStackedWidget>(alice, "sessionPages")->currentIndex() == 0);
    ap.authenticate("password123", true); bp.authenticate("password123", true); REQUIRE(wait([&] { return ap.online() && bp.online(); }));
    for (auto* name : {"messagesNav", "contactsNav", "filesNav", "accountMenu", "mainMenu"}) CHECK(widget<QWidget>(alice, name)->isVisible());
    screenshot(alice, output / "online-before-selection.png"); ap.selectConversation("qq_bob"); bp.selectConversation("qq_alice");
    auto* input = widget<QPlainTextEdit>(alice, "chatInput");
    CHECK(widget<QLineEdit>(alice, "chatPeer")->isReadOnly());
    if (QSystemTrayIcon::isSystemTrayAvailable()) bob.close();
    // Composition and Shift+Return stay in the editor; bare Return submits.
    input->setPlainText(QStringLiteral("中文首条消息"));
    QInputMethodEvent preedit(QStringLiteral("zhong"), {}); QCoreApplication::sendEvent(input, &preedit);
    QTest::keyClick(input, Qt::Key_Return); CHECK_FALSE(input->toPlainText().isEmpty());
    QInputMethodEvent commit; commit.setCommitString(QStringLiteral("中")); QCoreApplication::sendEvent(input, &commit);
    QTest::keyClick(input, Qt::Key_Return); CHECK(widget<QLabel>(alice,"composerFeedback")->text().isEmpty()); QTest::qWait(140);
    input->setPlainText(QStringLiteral("中文首条消息")); input->moveCursor(QTextCursor::End); QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier); CHECK(input->toPlainText().contains('\n'));
    QTest::keyClick(input, Qt::Key_Return); REQUIRE(wait([&] { return ap.identityConfirmationPending(); })); CHECK_FALSE(input->toPlainText().isEmpty());
    ap.confirmIdentity();
    if (QSystemTrayIcon::isSystemTrayAvailable()) { REQUIRE(wait([&] { return bob.property("deferredIdentityCount").toInt()>0; })); CHECK_FALSE(bp.identityConfirmationPending()); bob.show(); }
    REQUIRE(wait([&] { return bp.identityConfirmationPending(); })); bp.confirmIdentity();
    REQUIRE(wait([&] { return ap.hasReceipt(QStringLiteral("中文首条消息\n")); })); CHECK(input->toPlainText().isEmpty());
    ap.selectConversation("qq_other"); input->setPlainText(QStringLiteral("另一份草稿")); ap.selectConversation("qq_bob"); input->setPlainText(QStringLiteral("私聊草稿\n第二行"));
    ap.selectConversation("qq_other"); CHECK(input->toPlainText() == QStringLiteral("另一份草稿")); ap.selectConversation("qq_bob"); CHECK(input->toPlainText() == QStringLiteral("私聊草稿\n第二行"));
    input->setPlainText(QString(66000, 'x')); QTest::keyClick(input, Qt::Key_Return); REQUIRE(wait([&] { return widget<QLabel>(alice, "composerFeedback")->text().contains(QStringLiteral("草稿已保留")); })); CHECK(input->toPlainText().size() == 66000); input->clear();
    // A late result must not erase a replacement draft or another conversation.
    input->setPlainText("submitted-before-switch"); QTest::keyClick(input, Qt::Key_Return); input->setPlainText("new-edit-after-submit");
    REQUIRE(wait([&] { return ap.hasReceipt("submitted-before-switch"); })); CHECK(input->toPlainText() == "new-edit-after-submit"); input->clear();
    ap.createGroup(QStringLiteral("课设讨论群"), "qq_bob"); REQUIRE(wait([&] { return ap.hasGroup() && bp.hasGroup(); }));
    ap.showGroups(); auto* groupInput = widget<QPlainTextEdit>(alice, "groupInput"); groupInput->setPlainText(QStringLiteral("群聊草稿")); ap.selectConversation("qq_bob"); ap.showGroups(); CHECK(groupInput->toPlainText() == QStringLiteral("群聊草稿"));
    ap.sendGroupMessage(QStringLiteral("群内真实消息")); bp.showGroups(); REQUIRE(wait([&] { return bp.hasMessage(QStringLiteral("群内真实消息")); }));
    CHECK_FALSE(widget<QTableWidget>(alice, "groupSendReport")->isVisible());
    ap.selectConversation("qq_bob"); bp.selectConversation("qq_alice");
    const auto source = output / "long-name-transfer-proof-abcdefghijklmnopqrstuvwxyz0123456789.bin";
    { std::ofstream file(source, std::ios::binary); const std::string bytes(5 * 1024 * 1024, 'Q'); file.write(bytes.data(), bytes.size()); }
    ap.setDirectFilesEnabled(false); bp.setDirectFilesEnabled(false); ap.selectFile(QString::fromStdWString(source.wstring()));
    ap.selectConversation("qq_other"); CHECK(widget<QLabel>(alice, "attachmentRecipient")->text().contains("qq_bob")); ap.sendSelectedFile(); ap.selectConversation("qq_bob");
    if (QSystemTrayIcon::isSystemTrayAvailable()) { bob.close(); CHECK_FALSE(bob.isVisible()); }
    const bool fileComplete = wait([&] { const auto card = bp.fileCard(); return card && card->complete; }, 30000);
    if (!fileComplete) {
        bob.show(); screenshot(alice,output/"file-failure-sender.png"); screenshot(bob,output/"file-failure-recipient.png");
        std::cout << "sender=" << ap.notification().toStdString() << " recipient=" << bp.notification().toStdString() << " senderFeedback=" << widget<QLabel>(alice,"composerFeedback")->text().toStdString() << '\n';
        if (const auto card=bp.fileCard()) std::cout << "recipient progress=" << card->percent << " id=" << card->id.toStdString() << '\n';
    }
    REQUIRE(fileComplete);
    std::filesystem::path received; for (const auto& file : std::filesystem::recursive_directory_iterator(second.dataDirectory)) if (file.is_regular_file() && file.path().filename().string().ends_with("_" + source.filename().string())) received = file.path();
    REQUIRE_FALSE(received.empty()); CHECK(cc::crypto::sha256(bytes(source)) == cc::crypto::sha256(bytes(received)));
    bob.show(); bob.raise(); bob.activateWindow(); QTest::qWait(50);
    // Generate real records beyond the initial window, rather than mocked text.
    for (int i = 0; i < 110; ++i) { ap.sendMessage(QStringLiteral("历史消息 %1\n第二行用于验证滚动位置").arg(i)); }
    REQUIRE(wait([&] { return bp.hasMessage(QStringLiteral("历史消息 109\n第二行用于验证滚动位置")); }));
    auto* area = widget<QScrollArea>(bob, "chatMessages"); CHECK(area->findChildren<QLabel*>("messageText").size() <= 100);
    area->verticalScrollBar()->setValue(0); QTest::qWait(30); auto* older = area->findChild<QPushButton*>("loadOlderMessages"); REQUIRE(older); area->ensureWidgetVisible(older); QTest::mouseClick(older, Qt::LeftButton); QTest::qWait(80);
    CHECK(area->findChildren<QLabel*>("messageText").size() > 100); CHECK(area->verticalScrollBar()->value() > 0);
    area->verticalScrollBar()->setValue(0); ap.sendMessage("new-message-while-reading"); REQUIRE(wait([&] { return bp.hasMessage("new-message-while-reading"); })); CHECK(area->verticalScrollBar()->value() < area->verticalScrollBar()->maximum() - 30); CHECK(widget<QPushButton>(bob, "newMessages")->isVisible());
    if (QSystemTrayIcon::isSystemTrayAvailable()) { bob.close(); ap.sendMessage("tray-background-message"); REQUIRE(wait([&] { return bob.property("unreadCount").toInt() > 0; })); bob.show(); bob.raise(); bob.activateWindow(); }
    QTest::mouseClick(widget<QPushButton>(bob, "newMessages"), Qt::LeftButton); REQUIRE(wait([&] {
        auto* list=widget<QListWidget>(bob,"recentConversations"); for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole).toString()=="peer:qq_alice") return list->item(i)->data(Qt::UserRole+1).toInt()==0; return false;
    }));
    for (const auto& size : {QSize(860,640), QSize(960,640), QSize(1366,768), QSize(1920,1080)}) for (bool dark : {false,true}) {
        alice.setMinimumSize(size); alice.resize(size); alice.move(0,0); auto* appearance = widget<QPushButton>(alice, "appearanceMode"); if (appearance->isChecked() != dark) appearance->click(); QTest::qWait(120);
        auto* send = widget<QPushButton>(alice, "sendMessage"); CHECK(send->isVisible()); CHECK(alice.rect().contains(QRect(send->mapTo(&alice,QPoint()),send->size()))); CHECK(input->width() >= 200);
        CHECK(widget<QWidget>(alice, "messengerNav")->width() == 64);
        screenshot(alice, output / ("messages-" + std::to_string(size.width()) + (dark ? "-dark.png" : "-light.png")));
        QTest::mouseClick(widget<QPushButton>(alice, "contactsNav"), Qt::LeftButton); CHECK(widget<QStackedWidget>(alice,"messengerPages")->currentIndex() == 1); screenshot(alice, output / ("contacts-" + std::to_string(size.width()) + (dark ? "-dark.png" : "-light.png")));
        QTest::mouseClick(widget<QPushButton>(alice, "filesNav"), Qt::LeftButton); CHECK(widget<QStackedWidget>(alice,"messengerPages")->currentIndex() == 2); screenshot(alice, output / ("files-" + std::to_string(size.width()) + (dark ? "-dark.png" : "-light.png")));
        QTest::mouseClick(widget<QPushButton>(alice, "messagesNav"), Qt::LeftButton);
    }
    input->setPlainText("draft-before-logout"); ap.logout(); CHECK(widget<QStackedWidget>(alice,"sessionPages")->currentIndex() == 0); for (auto* name : {"messagesNav","contactsNav","filesNav","accountMenu","mainMenu"}) CHECK_FALSE(widget<QWidget>(alice,name)->isVisible());
    ap.authenticate("password123",false); REQUIRE(wait([&]{return ap.online();})); ap.selectConversation("qq_bob"); CHECK(input->toPlainText() == "draft-before-logout");
    std::cout << "QQ workspace scale=" << qEnvironmentVariable("QT_SCALE_FACTOR").toStdString() << " evidence=" << output.string() << '\n';
}
TEST_CASE("Worker local results correlate rejected commands and no-service workspace without replacing legacy events") {
    gui::BusinessWorker worker; REQUIRE(worker.post(SendText{"bob","hello"},"rejected-text")); REQUIRE(worker.post(gui::SetWorkspace{},"rejected-save"));
    std::set<std::string> rejected; REQUIRE(wait([&] { for (const auto& event : worker.drain()) if (const auto* result=std::get_if<gui::CommandResult>(&event)) { CHECK_FALSE(result->accepted); CHECK(result->error.has_value()); rejected.insert(result->requestId); } return rejected.size()==2; })); worker.requestStop(); worker.join();
}
int main(int argc,char** argv) { QApplication app(argc,argv); QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); return doctest::Context(argc,argv).run(); }
