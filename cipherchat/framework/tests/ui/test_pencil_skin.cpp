#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "qt_client_page.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <fstream>
#include <iostream>
using namespace ccfw;
namespace {
template<class T> T* widget(QWidget& root,const char* name){auto* result=root.findChild<T*>(name);REQUIRE(result);return result;}
bool wait(const std::function<bool()>& check){QElapsedTimer timer;timer.start();while(timer.elapsed()<10000){if(check())return true;QTest::qWait(20);}return check();}
void click(QWidget* target){INFO(target->objectName().toStdString());REQUIRE(target->isVisible());REQUIRE(target->isEnabled());QTest::mouseClick(target,Qt::LeftButton);QTest::qWait(40);}
void menu(QWidget& root,const char* name,const QString& text){auto* b=widget<QPushButton>(root,name);auto* popup=b->menu();REQUIRE(popup);QAction* selected=nullptr;for(auto* action:popup->actions())if(action->text()==text)selected=action;REQUIRE(selected);root.raise();root.activateWindow();REQUIRE(QTest::qWaitForWindowActive(&root));popup->popup(b->mapToGlobal(QPoint(0,b->height())));QTest::qWait(30);REQUIRE(popup->isVisible());QTest::mouseClick(popup,Qt::LeftButton,Qt::NoModifier,popup->actionGeometry(selected).center());QTest::qWait(50);}
QDialog* settings(gui::MainWindow& window){menu(window,"mainMenu",QStringLiteral("设置"));auto* dialog=widget<QDialog>(window,"settingsDialog");REQUIRE(dialog->isVisible());dialog->findChild<QTabWidget*>()->setCurrentIndex(0);return dialog;}
void chooseSkin(gui::MainWindow& window,bool pencil){auto* dialog=settings(window);auto* combo=widget<QComboBox>(*dialog,"skinChoice");combo->setFocus();QTest::keyClick(combo,pencil?Qt::Key_End:Qt::Key_Home);QTest::qWait(150);CHECK(combo->currentData().toString()==(pencil?"pencil":"classic"));dialog->close();QTest::qWait(80);}
void appearance(gui::MainWindow& window,bool dark){auto* dialog=settings(window);auto* mode=widget<QPushButton>(*dialog,"appearanceMode");if(mode->isChecked()!=dark)click(mode);dialog->close();QTest::qWait(80);}
void shot(QWidget& window,const std::filesystem::path& file){REQUIRE(window.grab().save(QString::fromStdWString(file.wstring())));}
std::string profile(const std::string& user){const auto hash=cc::crypto::sha256("real-test-server/"+user);return QByteArray(hash.data(),static_cast<int>(hash.size())).toHex().toStdString();}
std::string bytes(const std::filesystem::path& file){std::ifstream stream(file,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
QWidget* bubble(gui::MainWindow& window,const QString& text){for(auto* label:window.findChildren<QLabel*>("messageText"))if(label->text()==text)return label->parentWidget();return nullptr;}
std::pair<QString,int> anchor(QScrollArea* area){for(auto* b:area->widget()->findChildren<QWidget*>())if(b->property("timelineKey").isValid()){int y=b->mapTo(area->viewport(),QPoint()).y();if(y+b->height()>0&&y<area->viewport()->height())return {b->property("timelineKey").toString(),y};}return {};}
}
TEST_CASE("Pencil skin preserves TLS conversations drafts selection scroll and saved appearance") {
    test::RealServer server;const auto root=std::filesystem::path(CCFW_PENCIL_TEST_ROOT);const auto scale=qEnvironmentVariable("QT_SCALE_FACTOR","1");const auto output=root/("scale-"+scale.toStdString());std::filesystem::create_directories(output);
    const auto profiles=root/("p"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()%1000000000));std::filesystem::create_directories(profiles/profile("pencil_alice"));
    const auto source=profiles/std::filesystem::path(u8"彩铅手帐 实验附件-abcdefghijklmno.txt");{std::ofstream file(source,std::ios::binary);file<<"isolated pencil theme file\n"<<std::string(4096,'P');}
    const QString incoming=QStringLiteral("把今天的小心情，寄给你。\n云朵信笺 ✨"),outgoing=QStringLiteral("收到啦！用彩铅写下的每一句，都要好好保存。");
    test::RealClient bob("pencil_bob");bob.registerAndLogin(server.options());
    {test::RealClient alice("pencil_alice",profiles/profile("pencil_alice"));alice.registerAndLogin(server.options());alice.trust(bob);bob.trust(alice);
     const auto first=alice.chat.sendText({bob.name,outgoing.toStdString(),0,"",""});REQUIRE(first);alice.settle(bob,first.value());
     const auto second=bob.chat.sendText({alice.name,incoming.toStdString(),0,"",""});REQUIRE(second);bob.settle(alice,second.value());
     alice.files.setDirectEnabled(false);bob.files.setDirectEnabled(false);const auto file=bob.files.start(alice.name,source);REQUIRE(file);
     REQUIRE(test::eventually([&]{alice.application.tick();bob.application.tick();auto a=alice.files.progress(file.value()),b=bob.files.progress(file.value());return a&&b&&a.value().complete&&b.value().complete;},15000));CHECK(bytes(source)==bytes(alice.files.progress(file.value()).value().path));
     alice.application.shutdown();auto snapshot=alice.store.load();REQUIRE(snapshot);auto next=snapshot.value();const auto oldest=next.history.front().timestamp-200000;
     for(int i=0;i<135;++i)next.history.push_back({"pencil-history-"+std::to_string(i),{},bob.name,alice.name,"已保存的手帐对话 "+std::to_string(i),0,oldest+i*100,DeliveryState::RecipientReceived});
     next.features.workspace.selectedPeer=bob.name;next.features.workspace.drafts["peer:"+bob.name]="留在纸上的草稿";const auto revision=next.revision++;REQUIRE(alice.store.commit(revision,next));}
    QTimer tick;QObject::connect(&tick,&QTimer::timeout,[&]{bob.application.tick();});tick.start(15);
    QSettings prefs(QString::fromStdWString((profiles/"preferences.ini").wstring()),QSettings::IniFormat);prefs.setValue("ui/skin","classic");prefs.setValue("ui/colors/codex","#467a9b");prefs.sync();
    gui::WindowOptions options;options.connection=server.options();options.dataDirectory=profiles;options.username="pencil_alice";options.accountServerId="real-test-server";options.frontend="codex";
    {gui::MainWindow window(options);window.setWindowTitle(QStringLiteral("CipherChat 彩铅主题验收（隔离账号）"));window.resize(1366,768);window.show();REQUIRE(QTest::qWaitForWindowExposed(&window));test::QtClientPage page(window);page.authenticate("password123",false);REQUIRE(wait([&]{return page.online()&&window.property("sessionAuthenticated").toBool();}));page.selectConversation("pencil_bob");REQUIRE(wait([&]{return bubble(window,incoming)&&bubble(window,outgoing);}));
     auto* input=widget<QPlainTextEdit>(window,"chatInput");auto* area=widget<QScrollArea>(window,"chatMessages");CHECK(input->toPlainText()==QStringLiteral("留在纸上的草稿"));CHECK(window.property("uiSkin").toString()=="classic");
     area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum()/2);QTest::qWait(50);const auto reading=anchor(area);REQUIRE(!reading.first.isEmpty());QPointer<QWidget> stable=bubble(window,incoming);const auto classic=stable->grab().toImage();
     chooseSkin(window,true);CHECK(stable==bubble(window,incoming));CHECK(window.property("uiSkin").toString()=="pencil");CHECK(input->toPlainText()==QStringLiteral("留在纸上的草稿"));const auto after=anchor(area);CHECK(after.first==reading.first);CHECK(std::abs(after.second-reading.second)<=4);CHECK(stable->property("pencilBubble").toBool());CHECK(stable->grab().toImage()!=classic);CHECK(window.palette().color(QPalette::Window)==QColor("#fffdf2"));
     chooseSkin(window,false);CHECK(window.property("themeAccent").toString()=="#467a9b");CHECK_FALSE(stable->property("pencilBubble").toBool());chooseSkin(window,true);
     auto* dialog=settings(window);CHECK_FALSE(widget<QPushButton>(*dialog,"settingsThemeColor")->isEnabled());shot(*dialog,output/"appearance.png");dialog->close();
     area->verticalScrollBar()->setValue(0);QTest::qWait(60);click(widget<QPushButton>(*area,"loadOlderMessages"));REQUIRE(wait([&]{return area->findChildren<QWidget*>("chatBubble").size()>100;}));
     area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());input->clear();page.sendMessage(QStringLiteral("新皮肤下的真实消息与签收"));REQUIRE(wait([&]{return page.hasReceipt(QStringLiteral("新皮肤下的真实消息与签收"));}));
     const auto reply=bob.chat.sendText({"pencil_alice","长消息也要完整显示：\n"+std::string(280,'A')+"\n换行、中文和标点都保留。",0,"",""});REQUIRE(reply);REQUIRE(wait([&]{return page.hasMessage(QStringLiteral("长消息也要完整显示：\n")+QString(280,'A')+QStringLiteral("\n换行、中文和标点都保留。"));}));input->setPlainText(QStringLiteral("切换皮肤不清空的草稿"));
     for(const auto& size:{QSize(860,640),QSize(960,640),QSize(1366,768),QSize(1920,1080)})for(bool night:{false,true}) {
         window.setMinimumSize(size);window.resize(size);window.move(0,0);appearance(window,night);area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());QTest::qWait(120);
         const auto suffix=std::to_string(size.width())+(night?"-dark.png":"-light.png");
         for(const auto* name:{"messagesNav","contactsNav","filesNav","newConversation","sendMessage","fileTransferToggle"}){auto* control=widget<QPushButton>(window,name);CHECK(control->isVisible());CHECK(window.rect().contains(QRect(control->mapTo(&window,QPoint()),control->size())));CHECK(control->width()>=control->fontMetrics().horizontalAdvance(control->text())+4);}
         for(auto* b:area->findChildren<QWidget*>())if(b->property("timelineKey").isValid()) {CHECK(b->property("pencilBubble").toBool());CHECK(b->width()<=area->viewport()->width());for(auto* text:b->findChildren<QLabel*>("messageText")){CHECK(text->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));CHECK(text->font().family().contains("KaiTi"));CHECK(b->rect().contains(QRect(text->mapTo(b,QPoint()),text->size())));}}
         for(auto* text:area->findChildren<QLabel*>("messageText"))if(text->text().contains(QString(280,'A'))) {auto* doc=text->findChild<QTextDocument*>();REQUIRE(doc);CHECK(doc->toPlainText()==text->text());CHECK(doc->findBlockByNumber(1).layout()->lineCount()>=2);CHECK(doc->size().width()<=text->width()+1);CHECK(doc->size().height()<=text->height()+1);}
         for(auto* time:widget<QListWidget>(window,"recentConversations")->findChildren<QLabel*>("recentTime")){auto* row=time->parentWidget();auto* viewport=widget<QListWidget>(window,"recentConversations")->viewport();INFO("Row width="<<row->width()<<" min="<<row->minimumWidth()<<" view="<<viewport->width()<<" time="<<time->mapTo(viewport,QPoint()).x());CHECK(row->rect().contains(QRect(time->mapTo(row,QPoint()),time->size())));CHECK(viewport->rect().contains(QRect(time->mapTo(viewport,QPoint()),time->size())));}
         shot(window,output/("chat-"+suffix));area->ensureWidgetVisible(bubble(window,incoming));QTest::qWait(50);shot(window,output/("clouds-"+suffix));area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
     }
     for(auto* text:area->findChildren<QLabel*>("messageText"))if(text->text().contains(QString(280,'A'))) {area->ensureWidgetVisible(text);QTest::qWait(40);QTest::mouseDClick(text,Qt::LeftButton,Qt::NoModifier,QPoint(20,text->fontMetrics().height()+7));CHECK(text->selectedText()==QString(280,'A'));}
     window.setMinimumSize(860,640);window.resize(1366,768);appearance(window,false);
     for(const auto& pair:std::vector<std::pair<QString,QString>>{{QStringLiteral("把今天的小心情，寄给你。"),QStringLiteral("收到啦，今天也要有一点小幸运 ✨")},{QStringLiteral("想把日常写成一本彩铅手帐。"),QStringLiteral("那就从这一页开始吧，慢慢写、慢慢画。")},{QStringLiteral("消息、文件和小小的问候，都装进云朵里。"),QStringLiteral("好呀，愿每一次相遇都有温柔的颜色。")}}){page.sendMessage(pair.first);REQUIRE(wait([&]{return page.hasReceipt(pair.first);}));const auto response=bob.chat.sendText({"pencil_alice",pair.second.toStdString(),0,"",""});REQUIRE(response);REQUIRE(wait([&]{return page.hasMessage(pair.second);}));}
     input->setPlainText(QStringLiteral("写点什么…"));window.raise();window.activateWindow();REQUIRE(QTest::qWaitForWindowActive(&window));QTest::qWait(250);auto* last=bubble(window,QStringLiteral("好呀，愿每一次相遇都有温柔的颜色。"));REQUIRE(last);area->ensureWidgetVisible(last);area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());QTest::qWait(50);shot(window,output/"theme-preview.png");input->setPlainText(QStringLiteral("切换皮肤不清空的草稿"));
     click(widget<QPushButton>(window,"contactsNav"));click(widget<QPushButton>(window,"temporarySection"));shot(window,output/"contacts.png");click(widget<QPushButton>(window,"filesNav"));shot(window,output/"files.png");
     page.selectConversation("pencil_bob");page.createGroup(QStringLiteral("彩铅手帐小组"),"pencil_alice,pencil_bob");REQUIRE(wait([&]{return page.hasGroup();}));page.sendGroupMessage(QStringLiteral("写在云朵里的群聊问候"));REQUIRE(wait([&]{for(auto* text:widget<QScrollArea>(window,"groupMessages")->findChildren<QLabel*>("messageText"))if(text->text()==QStringLiteral("写在云朵里的群聊问候"))return true;return false;}));QTest::qWait(200);shot(window,output/"group.png");
     auto* group=widget<QScrollArea>(window,"groupMessages");for(auto* b:group->findChildren<QWidget*>("chatBubble"))CHECK(b->property("pencilBubble").toBool());
     page.selectConversation("pencil_bob");CHECK(input->toPlainText()==QStringLiteral("切换皮肤不清空的草稿"));page.logout();shot(window,output/"login.png");click(widget<QPushButton>(window,"registerButton"));shot(window,output/"registration.png");click(widget<QPushButton>(window,"backToLogin"));CHECK_FALSE(widget<QPushButton>(window,"contactsNav")->isVisible());}
    {gui::MainWindow window(options);window.show();REQUIRE(wait([&]{return window.property("uiSkin").toString()=="pencil";}));test::QtClientPage page(window);page.authenticate("password123",false);REQUIRE(wait([&]{return page.online()&&window.property("sessionAuthenticated").toBool();}));page.selectConversation("pencil_bob");CHECK(widget<QPlainTextEdit>(window,"chatInput")->toPlainText()==QStringLiteral("切换皮肤不清空的草稿"));chooseSkin(window,false);page.logout();}
    {gui::MainWindow window(options);window.show();CHECK(window.property("uiSkin").toString()=="classic");CHECK(window.property("themeAccent").toString()=="#467a9b");}
    tick.stop();bob.application.shutdown();std::cout<<"Pencil scale="<<scale.toStdString()<<" evidence="<<output.string()<<'\n';
}
int main(int argc,char** argv){QApplication app(argc,argv);return doctest::Context(argc,argv).run();}
