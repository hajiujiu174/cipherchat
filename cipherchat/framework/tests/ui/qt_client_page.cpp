#include "qt_client_page.hpp"
#include "main_window.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QMenu>
#include <QTimer>
#include <QFileDialog>
#include <QDir>
#include <QStackedWidget>
#include <QElapsedTimer>
#include <stdexcept>

namespace ccfw::test {
namespace {
template<class T> T* control(QObject& root, const char* name) {
    auto* found = root.findChild<T*>(QString::fromLatin1(name));
    if (!found) throw std::runtime_error(std::string("missing UI control: ") + name);
    return found;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void fill(QWidget* field, const QString& text) {
    if (!field->isVisible() || !field->isEnabled()) throw std::runtime_error("input unavailable: " + field->objectName().toStdString());
    field->setFocus(); QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QInputMethodEvent event; event.setCommitString(text); QCoreApplication::sendEvent(field, &event);
}
void click(QObject& root, const char* name) {
    auto* button = control<QAbstractButton>(root, name);
    require(button->isVisible() && button->isEnabled(), "action unavailable");
    QTest::mouseClick(button, Qt::LeftButton);
}
void menuAction(QWidget& root, const char* name, const QString& text) {
    root.raise(); root.activateWindow(); QTest::qWaitForWindowActive(&root, 2000);
    auto* button = control<QPushButton>(root, name); require(button->isVisible(), "menu entry is hidden");
    auto* menu = button->menu(); require(menu, "menu missing"); QAction* action = nullptr;
    for (auto* candidate : menu->actions()) if (candidate->text() == text) action = candidate;
    require(action, "menu action missing");
    menu->popup(button->mapToGlobal(QPoint(0,button->height()))); QTest::qWait(30);
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center()); QTest::qWait(40);
}
void logoutWindow(gui::MainWindow& window) {
    if (!window.property("sessionAuthenticated").toBool()) return;
    menuAction(window, "accountMenu", QStringLiteral("退出登录"));
    QElapsedTimer time; time.start(); while (window.property("sessionAuthenticated").toBool() && time.elapsed() < 7000) QTest::qWait(20);
    require(!window.property("sessionAuthenticated").toBool(), "logout did not complete");
}
// Discover the page containing a capability. Neither tab captions nor tab
// count/order are a contract. The production UI hides the native tab bar and
// exposes sidebar buttons tagged with "sideNavIndex"; clicking that button is a
// real user action. Older layouts with a visible tab bar still work via the
// fallback below.
void reveal(QWidget* target) {
    std::vector<std::pair<QTabWidget*,int>> pages;
    for (auto* ancestor = target->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto* tabs = qobject_cast<QTabWidget*>(ancestor)) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->widget(i) != target && !tabs->widget(i)->isAncestorOf(target)) continue;
                pages.emplace_back(tabs,i); break;
            }
        }
    }
    for (auto it=pages.rbegin();it!=pages.rend();++it) {
        auto* tabs=it->first; const auto selected=it->second; bool clicked=false;
        for(auto* button:tabs->window()->findChildren<QAbstractButton*>()) {
            const auto index=button->property("sideNavIndex");
            if(button->property("sideNavTarget").value<QObject*>()==tabs&&index.isValid()&&index.toInt()==selected&&button->isVisible()){QTest::mouseClick(button,Qt::LeftButton);clicked=true;break;}
        }
        if(!clicked&&tabs->tabBar()->isVisible())QTest::mouseClick(tabs->tabBar(),Qt::LeftButton,Qt::NoModifier,tabs->tabBar()->tabRect(selected).center());
    }
}
QDialog* peerDialog(QObject& root) {
    for (auto* dialog : root.findChildren<QDialog*>("identityDialog")) if (dialog->isVisible()) return dialog;
    throw std::runtime_error("identity confirmation is not open");
}
}
void QtClientPage::authenticate(const QString& password, bool registerNew) {
    logoutWindow(window_);
    if (control<QWidget>(window_, "registrationPage")->isVisible()) click(window_, "backToLogin");
    if (registerNew) {
        const auto user = control<QLineEdit>(window_, "username")->text();
        click(window_, "registerButton");
        fill(control<QLineEdit>(window_, "registrationUsername"), user);
        fill(control<QLineEdit>(window_, "registrationPassword"), password);
        fill(control<QLineEdit>(window_, "registrationConfirmation"), password);
        click(window_, "submitRegistration");
    } else {
        auto* input = control<QLineEdit>(window_, "password"); reveal(input); fill(input, password);
        click(window_, "loginButton");
    }
}
// Codex is the only desktop design; confirm it rather than clicking a removed switch.
void QtClientPage::useCodexDesign() {
    if (window_.property("frontendDesign").toString() != "codex") throw std::runtime_error("desktop client is not using Codex");
}
void QtClientPage::useDarkMode() {
    auto* appearance = control<QAbstractButton>(window_, "appearanceMode");
    if (!appearance->isChecked()) {
        if (!window_.property("sessionAuthenticated").toBool()) appearance->click();
        else { menuAction(window_, "mainMenu", QStringLiteral("设置")); control<QDialog>(window_, "settingsDialog")->findChild<QTabWidget*>()->setCurrentIndex(0); click(window_, "appearanceMode"); control<QDialog>(window_, "settingsDialog")->close(); }
    }
}
bool QtClientPage::online() const {
    return control<QLabel>(window_, "accountStatus")->text().startsWith(QStringLiteral("已登录"));
}
void QtClientPage::openOwnSecurity() { menuAction(window_, "accountMenu", QStringLiteral("我的账号与身份资料")); auto& card=ownSecuritySurface(); if (!control<QLabel>(card,"ownFingerprint")->isVisible()) click(card,"ownSecurityDetails"); }
void QtClientPage::logout() { logoutWindow(window_); }
QString QtClientPage::connectionHost() const { return control<QLineEdit>(window_, "serverHost")->text(); }
QWidget& QtClientPage::ownSecuritySurface() const {
    auto* dialog = control<QDialog>(window_, "ownIdentityDialog");
    require(dialog->isVisible(), "own security details not visible"); return *dialog;
}
QString QtClientPage::ownFingerprint() const {
    const auto value = control<QLabel>(ownSecuritySurface(), "ownFingerprint")->text();
    require(!value.isEmpty() && value != QStringLiteral("登录后显示"), "own identity missing after login"); return value;
}
void QtClientPage::closeOwnSecurity() {
    click(ownSecuritySurface(), "closeOwnSecurity");
    if (control<QWidget>(window_, "advancedSettingsScroll")->isVisible()) click(window_, "advancedSettingsToggle");
}
bool QtClientPage::identityConfirmationPending() const {
    for (auto* dialog : window_.findChildren<QDialog*>("identityDialog")) if (dialog->isVisible()) return true;
    return false;
}
QWidget& QtClientPage::peerSecuritySurface() const { return *peerDialog(window_); }
void QtClientPage::openPeerSecurity() {
    auto& dialog = peerSecuritySurface();
    if (!control<QLabel>(dialog, "peerFingerprint")->isVisible()) click(dialog, "peerSecurityDetails");
}
QString QtClientPage::peerFingerprint() const {
    auto* value = control<QLabel>(peerSecuritySurface(), "peerFingerprint");
    require(value->isVisible(), "peer fingerprint not visible"); return value->text();
}
void QtClientPage::confirmIdentity() { openPeerSecurity(); click(peerSecuritySurface(), "confirmIdentity"); }
void QtClientPage::selectConversation(const QString& peer) {
    if (peer.isEmpty()) { window_.openConversation({}); return; }
    window_.navigate(gui::MessengerPage::Messages);
    auto* list = control<QListWidget>(window_, "recentConversations");
    for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString() == "peer:" + peer) { list->scrollToItem(list->item(i)); QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(i)).center()); return; }
    menuAction(window_, "newConversation", QStringLiteral("发起聊天"));
    auto* dialog = control<QDialog>(window_, "newChatDialog"); fill(control<QLineEdit>(*dialog, "newChatPeer"), peer); click(*dialog, "confirmNewChat");
}
void QtClientPage::sendMessage(const QString& text) {
    fill(control<QPlainTextEdit>(window_, "chatInput"), text); click(window_, "sendMessage");
    // Wait for the local submission acknowledgement, not for delivery. This
    // preserves the original business assertions and avoids racing the next
    // synthetic button click with the pending submission guard.
    QTest::qWait(60);
}
bool QtClientPage::hasMessage(const QString& text) const {
    for (auto* label : window_.findChildren<QLabel*>("messageText")) if (label->isVisible() && label->text() == text) return true;
    return false;
}
bool QtClientPage::hasReceipt(const QString& text) const {
    for (auto* label : window_.findChildren<QLabel*>("messageText")) {
        if (!label->isVisible() || label->text() != text) continue;
        const auto* detail = label->parentWidget()->findChild<QLabel*>("messageDelivery");
        if (detail && detail->isVisible() && detail->text().contains(QStringLiteral("已签收"))) return true;
    }
    return false;
}
bool QtClientPage::attachmentsVisible() const { return control<QWidget>(window_, "conversationFiles")->isVisible(); }
void QtClientPage::showAttachments(bool visible) {
    window_.navigate(gui::MessengerPage::Messages);
    if (attachmentsVisible() != visible) click(window_, "fileTransferToggle");
    require(attachmentsVisible() == visible, "attachment tools did not change visibility");
}
void QtClientPage::selectFile(const QString& path) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); showAttachments(true);
    QTimer::singleShot(100, &window_, [this, path] {
        auto* dialog = window_.findChild<QFileDialog*>(); require(dialog, "file picker did not open");
        auto* name = dialog->findChild<QLineEdit*>("fileNameEdit"); require(name, "file picker filename missing"); fill(name, path); QTest::keyClick(name, Qt::Key_Return);
    });
    click(window_, "browseFile"); require(QDir::fromNativeSeparators(control<QLineEdit>(window_, "filePath")->text()) == QDir::fromNativeSeparators(path), "selected file path differs");
}
QString QtClientPage::selectedFile() const { return control<QLineEdit>(window_, "filePath")->text(); }
QString QtClientPage::selectedFileRecipient() const { return control<QLabel>(window_, "attachmentRecipient")->text(); }
bool QtClientPage::canSendFile() const { const auto* action = control<QAbstractButton>(window_, "sendFile"); return action->isEnabled() && action->isVisible(); }
void QtClientPage::sendSelectedFile() { click(window_, "sendFile"); }
std::optional<FileCardView> QtClientPage::fileCard() const {
    auto* card = control<QScrollArea>(window_, "chatMessages")->findChild<QWidget*>("fileCard");
    if (!card) return std::nullopt;
    auto* bar = control<QProgressBar>(*card, "fileProgress");
    auto available = [&](const char* name) { auto* button = control<QAbstractButton>(*card, name); return button->isEnabled() && button->isVisible(); };
    return FileCardView{card->property("fileId").toString(), card->property("timestamp").toLongLong(),
        bar->value(), bar->property("transportPath").toInt(), bar->value() == 100 && bar->format().contains(QStringLiteral("完成")),
        available("resumeFile"), available("cancelFile")};
}
bool QtClientPage::timelineOrder(const QString& before, const QString& after) const {
    auto* area = control<QScrollArea>(window_, "chatMessages");
    auto* card = area->findChild<QWidget*>("fileCard"); QWidget *first = nullptr, *last = nullptr;
    for (auto* text : area->findChildren<QLabel*>("messageText")) {
        if (text->text() == before) first = text->parentWidget();
        if (text->text() == after) last = text->parentWidget();
    }
    if (!card || !first || !last) return false;
    const auto stamp = card->property("timestamp").toLongLong();
    require(stamp > 0 && first->property("timestamp").toLongLong() <= stamp && stamp < last->property("timestamp").toLongLong(), "file timestamp is not between messages");
    require(first->y() < card->y() && card->y() < last->y(), "rendered timeline order differs from message order");
    return true;
}
void QtClientPage::showGroups() {
    auto* picker = control<QComboBox>(window_, "groupPicker");
    if (picker->currentIndex() >= 0) window_.openConversation({gui::ConversationTarget::Kind::Group, {}, picker->currentData().toLongLong()});
}
void QtClientPage::createGroup(const QString& name, const QString& members) {
    window_.navigate(gui::MessengerPage::Messages); menuAction(window_, "newConversation", QStringLiteral("创建群聊")); fill(control<QLineEdit>(window_, "groupName"), name); fill(control<QLineEdit>(window_, "groupMembers"), members); click(window_, "confirmCreateGroup");
}
bool QtClientPage::hasGroup() const { return control<QComboBox>(window_, "groupPicker")->count() > 0; }
void QtClientPage::sendGroupMessage(const QString& text) { showGroups(); fill(control<QPlainTextEdit>(window_, "groupInput"), text); click(window_, "sendGroup"); }
bool QtClientPage::directFilesEnabled() const { return control<QCheckBox>(window_, "allowDirectFiles")->isChecked(); }
void QtClientPage::setDirectFilesEnabled(bool enabled) {
    menuAction(window_, "mainMenu", QStringLiteral("设置")); control<QDialog>(window_, "settingsDialog")->findChild<QTabWidget*>()->setCurrentIndex(2);
    if (directFilesEnabled() != enabled) {
        auto* check = control<QCheckBox>(window_, "allowDirectFiles");
        require(check->isVisible() && check->isEnabled(), "direct file preference unavailable");
        // The center of a stretched checkbox may be outside its hit region.
        // Keyboard activation is a real user action and independent of width.
        check->setFocus(); QTest::keyClick(check, Qt::Key_Space);
    }
    require(directFilesEnabled() == enabled, "direct file preference did not change"); control<QDialog>(window_, "settingsDialog")->close();
}
QString QtClientPage::notification() const { return control<QLabel>(window_, "notification")->text(); }
void QtClientPage::checkAccountLayout() const {
    for (const auto* name : {"serverHost", "serverPort", "dataDirectory", "inspectIdentity"})
        require(!control<QWidget>(window_, name)->isVisible(), "connection details visible by default");
    for (const auto* name : {"caFile", "browseCA", "serverName", "ownFingerprint"}) {
        auto* value = window_.findChild<QWidget*>(name);
        require(!value || !value->isVisible(), "sensitive configuration visible on login page");
    }
}
void QtClientPage::checkAttachmentsCollapsed() const {
    require(!attachmentsVisible() && !control<QWidget>(window_, "filePath")->isVisible(), "attachments should start collapsed");
    require(control<QWidget>(window_, "sendMessage")->isVisible(), "message composer is hidden");
}
void QtClientPage::checkPeerDetailsInitiallyHidden() const {
    // UID identifies the account; trusting keys still requires opening their fingerprint.
    require(!control<QLabel>(peerSecuritySurface(), "peerFingerprint")->isVisible(), "identity card security details unexpectedly expanded");
    require(control<QAbstractButton>(peerSecuritySurface(), "confirmIdentity")->isVisible(), "explicit identity confirmation unavailable");
}
void QtClientPage::scrollFileIntoView() {
    control<QScrollArea>(window_, "chatMessages")->ensureWidgetVisible(control<QWidget>(window_, "fileCard"), 0, 8);
}
void QtClientPage::checkFileFitsViewport() const {
    auto* area = control<QScrollArea>(window_, "chatMessages"); auto* card = control<QWidget>(window_, "fileCard");
    // Delivery events can rebuild the timeline between timer ticks. Scroll the
    // current card, not the instance that was present before the last drain.
    area->ensureWidgetVisible(card, 0, 8);
    auto* viewport = area->viewport();
    const auto top = card->mapTo(viewport, QPoint(0, 0));
    if (top.y() < 0 || top.y() + card->height() > viewport->height())
        throw std::runtime_error("file card cannot be viewed at minimum window size: top=" + std::to_string(top.y()) +
            ", height=" + std::to_string(card->height()) + ", viewport=" + std::to_string(viewport->height()));
}
}
