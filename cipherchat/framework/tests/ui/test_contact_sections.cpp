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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace ccfw;
namespace {
template<class T> T* widget(QWidget& root,const char* name) { auto* result=root.findChild<T*>(name); REQUIRE(result); return result; }
bool wait(const std::function<bool()>& check) { QElapsedTimer timer;timer.start();while(timer.elapsed()<9000){if(check())return true;QTest::qWait(20);}return check(); }
void click(QPushButton* button) { REQUIRE(button);INFO("Click: "<<button->objectName().toStdString());REQUIRE(button->isVisible());REQUIRE(button->isEnabled());QTest::mouseClick(button,Qt::LeftButton);QTest::qWait(60); }
QPushButton* action(QWidget& root,const char* name,const QString& username) { for(auto* button:root.findChildren<QPushButton*>(name))if(button->property("username").toString()==username)return button;return nullptr; }
QListWidgetItem* item(QListWidget* list,const QString& username) { for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toString()==username)return list->item(i);return nullptr; }
void select(QListWidget* list,QListWidgetItem* row,bool twice=false) { REQUIRE(row);REQUIRE(list->isVisible());REQUIRE_FALSE(row->isHidden());list->scrollToItem(row);QTest::qWait(30);const auto position=list->visualItemRect(row).center();QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,position);if(twice)QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,position);QTest::qWait(60); }
void shot(QWidget& root,const std::filesystem::path& file) { REQUIRE(root.grab().save(QString::fromStdWString(file.wstring()))); }
std::string profile(const std::string& name) { const auto bytes=cc::crypto::sha256("real-test-server/"+name);return QByteArray(bytes.data(),static_cast<int>(bytes.size())).toHex().toStdString(); }
}
TEST_CASE("Restored contact sections use real discovery temporary messages and approved friendship") {
    test::RealServer server;const auto root=std::filesystem::path(CCFW_CONTACT_SECTIONS_ROOT);
    const auto scale=qEnvironmentVariable("QT_SCALE_FACTOR","1");const auto output=root/("scale-"+scale.toStdString());std::filesystem::create_directories(output);
    const auto profiles=root/("p"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()%1000000000));std::filesystem::create_directories(profiles);
    const std::string aName="contacts_alice",bName="contacts_bob";
    std::filesystem::create_directories(profiles/profile(aName));
    test::RealClient bob(bName);bob.registerAndLogin(server.options());
    { test::RealClient alice(aName,profiles/profile(aName));alice.registerAndLogin(server.options());alice.trust(bob);bob.trust(alice);
      const auto sent=alice.chat.sendText({bName,"原来的临时会话记录",0,""});REQUIRE(sent);alice.settle(bob,sent.value()); }
    { test::RealClient visitor("contacts_visitor");visitor.registerAndLogin(server.options()); }
    QTimer tick;QObject::connect(&tick,&QTimer::timeout,[&]{bob.application.tick();});tick.start(15);
    gui::WindowOptions options;options.connection=server.options();options.dataDirectory=profiles;options.username=aName;options.accountServerId="real-test-server";options.frontend="codex";
    gui::MainWindow window(options);window.setWindowTitle(QStringLiteral("CipherChat 联系人验收（隔离账号）"));window.show();REQUIRE(QTest::qWaitForWindowExposed(&window));test::QtClientPage page(window);
    for(const auto* name:{"friendsSection","groupsSection","requestsSection","temporarySection","strangersSection"})CHECK_FALSE(widget<QPushButton>(window,name)->isVisible());
    auto authenticated=[&]{return page.online()&&window.property("sessionAuthenticated").toBool()&&widget<QPushButton>(window,"contactsNav")->isVisible();};
    page.authenticate("password123",false);REQUIRE(wait(authenticated));
    auto* sections=widget<QComboBox>(window,"directorySection");auto* pages=widget<QStackedWidget>(window,"messengerPages");auto* temporary=widget<QListWidget>(window,"temporaryChats");
    auto* friends=widget<QListWidget>(window,"contacts");auto* search=widget<QLineEdit>(window,"contactSearch");auto* query=widget<QLineEdit>(window,"strangerQuery");
    auto* directory=qobject_cast<QStackedWidget*>(temporary->parentWidget());REQUIRE(directory);
    auto contacts=[&]{click(widget<QPushButton>(window,"contactsNav"));};contacts();
    const char* names[]{"friendsSection","groupsSection","requestsSection","temporarySection","strangersSection"};const int indexes[]{0,4,3,1,2};
    for(int i=0;i<5;++i){auto* button=widget<QPushButton>(window,names[i]);click(button);CHECK(button->isChecked());CHECK(sections->currentIndex()==indexes[i]);CHECK(directory->currentIndex()==indexes[i]);}
    click(widget<QPushButton>(window,"temporarySection"));REQUIRE(wait([&]{return item(temporary,"contacts_bob");}));CHECK(friends->count()==0);
    window.raise();window.activateWindow();REQUIRE(QTest::qWaitForWindowActive(&window));QTest::keyClick(&window,Qt::Key_F,Qt::ControlModifier);CHECK(search->hasFocus());
    QTest::keyClicks(search,"missing");CHECK(item(temporary,"contacts_bob")->isHidden());search->clear();QTest::keyClicks(search,"BOB");CHECK_FALSE(item(temporary,"contacts_bob")->isHidden());
    select(temporary,item(temporary,"contacts_bob"));CHECK(pages->currentIndex()==1);CHECK(widget<QLabel>(window,"contactDetails")->text().contains("contacts_bob"));
    click(widget<QPushButton>(window,"contactStartChat"));CHECK(pages->currentIndex()==0);CHECK(widget<QLineEdit>(window,"chatPeer")->text()=="contacts_bob");CHECK(page.hasMessage(QStringLiteral("原来的临时会话记录")));
    auto* draft=widget<QPlainTextEdit>(window,"chatInput");draft->setPlainText(QStringLiteral("切换分类保留草稿"));contacts();click(widget<QPushButton>(window,"temporarySection"));select(temporary,item(temporary,"contacts_bob"),true);CHECK(pages->currentIndex()==0);CHECK(draft->toPlainText()==QStringLiteral("切换分类保留草稿"));
    contacts();click(widget<QPushButton>(window,"strangersSection"));CHECK_FALSE(search->isVisible());CHECK(query->isVisible());
    QTest::keyClick(&window,Qt::Key_F,Qt::ControlModifier);CHECK(query->hasFocus());QTest::keyClicks(query,"missing");QTest::keyClick(query,Qt::Key_Return);REQUIRE(wait([&]{return widget<QListWidget>(window,"strangers")->count()==0;}));
    query->selectAll();QTest::keyClicks(query,"BOB");QTest::keyClick(query,Qt::Key_Return);REQUIRE(wait([&]{return action(window,"temporaryChat","contacts_bob");}));
    click(action(window,"temporaryChat","contacts_bob"));CHECK(pages->currentIndex()==0);CHECK(draft->toPlainText()==QStringLiteral("切换分类保留草稿"));CHECK(friends->count()==0);
    draft->clear();page.sendMessage(QStringLiteral("恢复入口后的真实加密消息"));REQUIRE(wait([&]{return page.hasReceipt(QStringLiteral("恢复入口后的真实加密消息"));}));
    const auto received=bob.store.load();REQUIRE(received);CHECK(std::any_of(received.value().history.begin(),received.value().history.end(),[](const auto& record){return record.text=="恢复入口后的真实加密消息";}));
    page.selectConversation("contacts_visitor");draft->setPlainText(QStringLiteral("尚未发送的临时草稿"));contacts();click(widget<QPushButton>(window,"temporarySection"));REQUIRE(item(temporary,"contacts_visitor"));search->clear();
    // Repeated category navigation keeps all stacked pages in place.
    click(widget<QPushButton>(window,"strangersSection"));CHECK(pages->currentIndex()==1);CHECK(sections->currentIndex()==2);QTest::keyClick(&window,Qt::Key_F,Qt::ControlModifier);CHECK(query->hasFocus());click(widget<QPushButton>(window,"temporarySection"));CHECK(temporary->isVisible());click(widget<QPushButton>(window,"strangersSection"));CHECK(widget<QListWidget>(window,"strangers")->isVisible());
    query->clear();click(widget<QPushButton>(window,"searchStrangers"));REQUIRE(wait([&]{return widget<QListWidget>(window,"strangers")->count()==2;}));
    for(const auto& size:{QSize(860,640),QSize(960,640),QSize(1366,768),QSize(1920,1080)})for(bool dark:{false,true}) {
        window.setMinimumSize(size);window.resize(size);window.move(0,0);auto* mode=widget<QPushButton>(window,"appearanceMode");if(mode->isChecked()!=dark)mode->click();QTest::qWait(100);
        for(const auto* category:{"temporarySection","strangersSection"}) {
            click(widget<QPushButton>(window,category));QTest::qWait(80);
            for(const auto* name:names){auto* button=widget<QPushButton>(window,name);CHECK(button->isVisible());CHECK(button->width()>=button->fontMetrics().horizontalAdvance(button->text())+6);CHECK(window.rect().contains(QRect(button->mapTo(&window,QPoint()),button->size())));}
            auto* list=widget<QListWidget>(window,std::string_view(category)=="temporarySection"?"temporaryChats":"strangers");CHECK(list->isVisible());CHECK(list->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff);
            if(list==widget<QListWidget>(window,"strangers"))for(int i=0;i<list->count();++i){auto* row=list->itemWidget(list->item(i));for(auto* button:row->findChildren<QPushButton*>()){CHECK(row->rect().contains(QRect(button->mapTo(row,QPoint()),button->size())));CHECK(button->width()>=button->fontMetrics().horizontalAdvance(button->text())+6);}}
            const auto suffix=std::string(category)+"-"+std::to_string(size.width())+(dark?"-dark.png":"-light.png");shot(window,output/suffix);
        }
    }
    window.setMinimumSize(860,640);window.resize(960,640);QTest::qWait(80);
    page.logout();for(const auto* name:names)CHECK_FALSE(widget<QPushButton>(window,name)->isVisible());shot(window,output/"logged-out.png");
    page.authenticate("password123",false);REQUIRE(wait(authenticated));contacts();click(widget<QPushButton>(window,"temporarySection"));REQUIRE(item(temporary,"contacts_visitor"));select(temporary,item(temporary,"contacts_visitor"),true);CHECK(draft->toPlainText()==QStringLiteral("尚未发送的临时草稿"));
    contacts();click(widget<QPushButton>(window,"strangersSection"));query->clear();click(widget<QPushButton>(window,"searchStrangers"));REQUIRE(wait([&]{return action(window,"addFriend","contacts_bob");}));click(action(window,"addFriend","contacts_bob"));
    REQUIRE(wait([&]{return server.database.friendRequests(bName,true)==std::vector<std::string>{aName};}));CHECK(friends->count()==0);REQUIRE(bob.application.respondFriend(aName,true));
    REQUIRE(wait([&]{return item(friends,"contacts_bob")&&!item(temporary,"contacts_bob");}));click(widget<QPushButton>(window,"friendsSection"));select(friends,item(friends,"contacts_bob"));CHECK(widget<QLabel>(window,"contactDetails")->text().contains("contacts_bob"));click(widget<QPushButton>(window,"contactStartChat"));CHECK(pages->currentIndex()==0);
    page.logout();shot(window,output/"login-after-logout.png");tick.stop();bob.application.shutdown();
    std::cout<<"Contact sections scale="<<scale.toStdString()<<" evidence="<<output.string()<<'\n';
}
int main(int argc,char** argv) { QApplication app(argc,argv);return doctest::Context(argc,argv).run(); }
