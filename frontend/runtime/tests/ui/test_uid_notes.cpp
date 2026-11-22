#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "feature_ui.hpp"
#include "qt_client_page.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <iostream>
using namespace ccfw;
namespace {
// 返回桌面控件的对应的 Qt 控件。
template<class T> T* widget(QWidget& root,const char* name) { auto* found=root.findChild<T*>(name); REQUIRE(found); return found; }
// 等待桌面控件。
bool wait(const std::function<bool()>& p) { QElapsedTimer t;t.start();while(t.elapsed()<10000){if(p())return true;QTest::qWait(20);}return p(); }
// 验证“click”场景中的关键结果，断言失败时由测试框架报告。
void click(QWidget* target) { REQUIRE(target->isVisible());REQUIRE(target->isEnabled());QTest::mouseClick(target,Qt::LeftButton); }
// 验证“menu”场景中的关键结果，断言失败时由测试框架报告。
void menu(QWidget& root,const char* name,const QString& text) {
    auto* b=widget<QPushButton>(root,name);auto* m=b->menu();REQUIRE(m);QAction* selected=nullptr;
    for(auto* a:m->actions())if(a->text()==text)selected=a;REQUIRE(selected);
    m->popup(b->mapToGlobal(QPoint(0,b->height())));QTest::qWait(30);QTest::mouseClick(m,Qt::LeftButton,Qt::NoModifier,m->actionGeometry(selected).center());
}
// 验证“卡片”场景中的关键结果，断言失败时由测试框架报告。
QDialog& card(gui::MainWindow& root) {
    menu(root, "conversationMenu", QStringLiteral("UID 与备注"));
    auto* result = widget<QDialog>(root, "identityDialog");
    REQUIRE(wait([&] { return result->isVisible(); }));
    return *result;
}
// 验证“notes”场景中的关键结果，断言失败时由测试框架报告。
void notes(QDialog& card) { auto* tabs=widget<QTabWidget>(card,"identityCardTabs");REQUIRE(tabs->count()==2);CHECK(tabs->tabText(0)=="UID");CHECK(tabs->tabText(1)==QStringLiteral("备注"));QTest::mouseClick(tabs->tabBar(),Qt::LeftButton,Qt::NoModifier,tabs->tabBar()->tabRect(1).center()); }
// 保存桌面控件。
void save(QDialog& card,const QString& text) { notes(card);widget<QPlainTextEdit>(card,"contactRemarkInput")->setPlainText(text);click(widget<QPushButton>(card,"saveContactRemark")); }
// 关闭桌面控件。
void close(QDialog& card) { click(widget<QPushButton>(card,"closePeerSecurity"));QTest::qWait(30); }
// 验证“shot”场景中的关键结果，断言失败时由测试框架报告。
void shot(QWidget& root,const std::filesystem::path& path) { REQUIRE(root.grab().save(QString::fromStdWString(path.wstring()))); }
}
// 验证用例“UID cards and local remarks use real accounts retain drafts and isolate profiles”所描述的行为。
TEST_CASE("UID cards and local remarks use real accounts retain drafts and isolate profiles") {
    const auto scale=qEnvironmentVariable("QT_SCALE_FACTOR").toStdString();const auto output=std::filesystem::path(CCFW_UID_TEST_ROOT)/("scale-"+scale);std::filesystem::create_directories(output);
    test::RealServer server;gui::WindowOptions options;options.connection=server.options();options.dataDirectory=test::tempDirectory("UID-notes-profile");options.username="notes_alice";
    auto second=options;second.dataDirectory=test::tempDirectory("UID-notes-bob");second.username="notes_bob";
    const auto remark=QStringLiteral("网络课设搭档 <b>原文</b>\n周五提交");
    {
        gui::MainWindow alice(options),bob(second);alice.show();bob.show();test::QtClientPage ap(alice),bp(bob);ap.authenticate("password123",true);bp.authenticate("password123",true);REQUIRE(wait([&]{return ap.online()&&bp.online();}));
        menu(alice, "accountMenu", QStringLiteral("我的账号")); auto& own=*widget<QDialog>(alice, "ownIdentityDialog");CHECK(widget<QLabel>(own,"identityUid")->text().toLongLong()==server.database.findAccount(options.username)->id);CHECK(own.findChild<QObject*>("identityQr")==nullptr);CHECK(own.findChild<QObject*>("identityCardInput")==nullptr);own.close();
        ap.selectConversation("notes_bob");bp.selectConversation("notes_alice");auto* draft=widget<QPlainTextEdit>(alice,"chatInput");draft->setPlainText(QStringLiteral("不要改动这份草稿"));
        auto& first=card(alice);REQUIRE(wait([&]{return widget<QLabel>(first,"identityUid")->text().toLongLong()==server.database.findAccount("notes_bob")->id;}));CHECK(first.findChild<QPushButton*>("confirmIdentity") == nullptr);save(first,remark);
        CHECK(widget<QLabel>(first,"contactRemarkResult")->text()==QStringLiteral("备注已保存"));CHECK(draft->toPlainText()==QStringLiteral("不要改动这份草稿"));close(first);
        auto* label=widget<QLabel>(alice,"chatRemark");CHECK(label->isVisible());CHECK(label->textFormat()==Qt::PlainText);CHECK(label->toolTip()==remark);CHECK(label->mapTo(&alice,QPoint()).y()>widget<QLineEdit>(alice,"chatPeer")->mapTo(&alice,QPoint()).y());
        ap.selectConversation("another_peer");CHECK_FALSE(label->isVisible());ap.selectConversation("notes_bob");CHECK(label->isVisible());CHECK(draft->toPlainText()==QStringLiteral("不要改动这份草稿"));
        auto& again=card(alice);notes(again);CHECK(widget<QPlainTextEdit>(again,"contactRemarkInput")->toPlainText()==remark);save(again,QString(1025,'x'));CHECK(widget<QLabel>(again,"contactRemarkResult")->text().contains("1024"));CHECK(widget<QPlainTextEdit>(again,"contactRemarkInput")->toPlainText().size()==1025);CHECK(label->toolTip()==remark);
        save(again,{});CHECK_FALSE(label->isVisible());save(again,remark);close(again);draft->clear();
        ap.sendMessage(QStringLiteral("备注保留，消息正常发送"));
        REQUIRE(wait([&]{return ap.hasReceipt(QStringLiteral("备注保留，消息正常发送"));}));
        for(const auto& size:{QSize(860,640),QSize(960,640),QSize(1366,768),QSize(1920,1080)})for(bool dark:{false,true}) {
            alice.setMinimumSize(size);alice.resize(size);alice.move(0,0);auto* appearance=widget<QPushButton>(alice,"appearanceMode");if(appearance->isChecked()!=dark)appearance->click();QTest::qWait(140);
            const auto suffix=std::to_string(size.width())+(dark?"-dark.png":"-light.png");CHECK(alice.rect().contains(QRect(label->mapTo(&alice,QPoint()),label->size())));shot(alice,output/("header-"+suffix));
            auto& display=card(alice);CHECK(display.findChild<QLabel*>("peerFingerprint") == nullptr);CHECK(display.rect().contains(QRect(widget<QLabel>(display,"identityUid")->mapTo(&display,QPoint()),widget<QLabel>(display,"identityUid")->size())));
            QTest::qWait(20);const auto rendered=display.grab();const auto background=rendered.toImage().pixelColor(qRound(500*rendered.devicePixelRatio()),qRound(300*rendered.devicePixelRatio()));
            CHECK(dark?background.lightness()<90:background.lightness()>180);CHECK(QFontMetrics(widget<QLabel>(display,"identityUid")->font()).height() >= 26);
            CHECK_FALSE(widget<QLabel>(alice,"composerFeedback")->text().contains(QStringLiteral("等待核对身份")));shot(display,output/("uid-"+suffix));notes(display);shot(display,output/("remark-"+suffix));close(display);
        }
        ap.logout();ap.authenticate("password123",false);REQUIRE(wait([&]{return ap.online();}));ap.selectConversation("notes_bob");CHECK(label->toolTip()==remark);ap.logout();bp.logout();
    }
    {
        gui::MainWindow restored(options);restored.show();test::QtClientPage p(restored);p.authenticate("password123",false);REQUIRE(wait([&]{return p.online();}));p.selectConversation("notes_bob");CHECK(widget<QLabel>(restored,"chatRemark")->toolTip()==remark);p.logout();
        // Same directory, different signed-in account must not inherit Alice's note.
        widget<QLineEdit>(restored,"username")->setText("notes_other");p.authenticate("password123",true);REQUIRE(wait([&]{return p.online();}));p.selectConversation("notes_bob");CHECK_FALSE(widget<QLabel>(restored,"chatRemark")->isVisible());p.logout();
    }
    {
        test::RealServer other;auto moved=options;moved.connection=other.options();gui::MainWindow isolated(moved);isolated.show();test::QtClientPage p(isolated);p.authenticate("password123",true);REQUIRE(wait([&]{return p.online();}));p.selectConversation("notes_bob");CHECK_FALSE(widget<QLabel>(isolated,"chatRemark")->isVisible());p.logout();
    }
    std::cout<<"UID notes scale="<<scale<<" evidence="<<output.string()<<'\n';
}
// 验证用例“Remark storage rejection and account switch preserve editor”所描述的行为。
TEST_CASE("Remark storage rejection and account switch preserve editor") {
    QWidget owner;int submits=0,saves=0;gui::FeatureUi ui(&owner,[&](gui::Command){++submits;return true;});gui::RestoredView view;view.scope={"server-a","alice"};ui.consume(gui::WorkerEvent{view});
    ui.saveRemark=[&](const std::string&,const QString&){++saves;return false;};auto* d=ui.contactCard("bob");notes(*d);save(*d,QStringLiteral("写入失败时保留"));CHECK(widget<QPlainTextEdit>(*d,"contactRemarkInput")->toPlainText()==QStringLiteral("写入失败时保留"));CHECK(widget<QLabel>(*d,"contactRemarkResult")->text().contains(QStringLiteral("失败")));CHECK(saves==1);CHECK(submits==0);
    view.scope.username="other";ui.consume(gui::WorkerEvent{view});click(widget<QPushButton>(*d,"saveContactRemark"));CHECK(widget<QLabel>(*d,"contactRemarkResult")->text().contains(QStringLiteral("账号已切换")));CHECK(saves==1);CHECK(submits==0);close(*d);
}
// 准备测试进程和输入，运行该文件验证的用例并返回退出状态。
int main(int argc,char** argv) { QApplication app(argc,argv);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);return doctest::Context(argc,argv).run(); }
