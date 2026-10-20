#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "local_file_actions.hpp"
#include "qt_client_page.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTest>
#include <fstream>
#include <iostream>
using namespace ccfw;
namespace {
template<class T> T* widget(QWidget& root,const char* name) { auto* result=root.findChild<T*>(name); REQUIRE(result); return result; }
bool wait(const std::function<bool()>& check,int ms=10000) { QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){if(check())return true;QTest::qWait(10);}return false; }
void click(QPushButton* action) { REQUIRE(action->isVisible());REQUIRE(action->isEnabled());QTest::mouseClick(action,Qt::LeftButton);QTest::qWait(60); }
QWidget* row(QWidget& root,const std::string& id) { for(auto* item:root.findChildren<QWidget*>("fileQueueRow")) if(item->property("fileId").toString()==QString::fromStdString(id))return item;return nullptr; }
std::string readBytes(const std::filesystem::path& file) { std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}}; }
void shot(QWidget& root,const std::filesystem::path& file) { REQUIRE(root.grab().save(QString::fromStdWString(file.wstring()))); }
std::string profile(const std::string& name) { const auto bytes=cc::crypto::sha256("real-test-server/"+name);return QByteArray(bytes.data(),static_cast<int>(bytes.size())).toHex().toStdString(); }
}
TEST_CASE("Codex file rows navigate exact old file and use correct local paths without losing drafts") {
    test::RealServer server;
    const auto scale=qEnvironmentVariable("QT_SCALE_FACTOR","1");
    const bool review=qEnvironmentVariableIsSet("CCFW_FILE_NATIVE_REVIEW");
    const auto root=std::filesystem::path(CCFW_FILE_TEST_ROOT);
    const auto output=root/((review?"native-":"scale-")+scale.toStdString());std::filesystem::create_directories(output);
    const auto profiles=root/("p"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()%1000000000));
    std::filesystem::create_directories(profiles);
    const auto source=profiles/std::filesystem::path(u8"课程实验报告, 最终稿（中文空格）-abcdefghijklmnopqrstuv.txt");
    {std::ofstream file(source,std::ios::binary);file<<"CipherChat isolated file action evidence\n"<<std::string(2048,'F');}
    const auto activeSource=profiles/"ongoing-file.bin";{std::ofstream file(activeSource,std::ios::binary);file<<std::string(5242880,'P');}
    std::string receivedId,cancelledId,pendingId,incomingId;std::filesystem::path received;
    std::filesystem::create_directories(profiles/profile("files_alice"));std::filesystem::create_directories(profiles/profile("files_bob"));
    {
        test::RealClient a("files_alice",profiles/profile("files_alice")),b("files_bob",profiles/profile("files_bob"));
        a.registerAndLogin(server.options());b.registerAndLogin(server.options());a.trust(b);b.trust(a);a.files.setDirectEnabled(false);b.files.setDirectEnabled(false);
        const auto id=b.files.start(a.name,source);REQUIRE(id);receivedId=id.value();
        REQUIRE(test::eventually([&]{a.application.tick();b.application.tick();const auto ap=a.files.progress(receivedId),bp=b.files.progress(receivedId);return ap&&bp&&ap.value().complete&&bp.value().complete;},15000));
        received=a.files.progress(receivedId).value().path;REQUIRE(readBytes(source)==readBytes(received));
        const auto incoming=b.files.start(a.name,activeSource);REQUIRE(incoming);incomingId=incoming.value();
        REQUIRE(test::eventually([&]{b.application.tick();a.application.tick();const auto progress=a.files.progress(incomingId);return progress&&!progress.value().complete;},10000));
        b.application.shutdown();
        const auto cancel=a.files.start(b.name,source);REQUIRE(cancel);cancelledId=cancel.value();REQUIRE(a.files.cancel(cancelledId));
        const auto pending=a.files.start(b.name,activeSource);REQUIRE(pending);pendingId=pending.value();
        a.application.shutdown();
        auto snapshot=a.store.load();REQUIRE(snapshot);auto next=snapshot.value();
        const auto timestamp=next.transfers.front().timestamp+5000;
        for(int i=0;i<135;++i) next.history.push_back({"saved-history-"+std::to_string(i),{},"files_bob","files_alice","较新的已保存消息 "+std::to_string(i),0,timestamp+i*100,DeliveryState::RecipientReceived});
        auto missing=next.transfers.front();missing.fileId="missing-file-proof";missing.finalPath=profiles/"deleted-proof.txt";missing.name="已移动或删除的文件.txt";missing.timestamp=timestamp+14000;next.transfers.push_back(missing);
        for(int i=0;i<6;++i) {auto archived=next.transfers.front();archived.fileId="archived-proof-"+std::to_string(i);archived.name="历史文件记录 "+std::to_string(i)+".txt";archived.timestamp-=i*10000+10000;next.transfers.push_back(archived);}
        next.features.workspace.selectedPeer="files_bob";next.features.workspace.drafts["peer:files_bob"]="文件操作期间保留的草稿";
        const auto revision=next.revision++;REQUIRE(a.store.commit(revision,next));
    }
    gui::WindowOptions options;options.connection=server.options();options.dataDirectory=profiles;options.username="files_alice";options.accountServerId="real-test-server";options.frontend="codex";
    QStringList opened,revealed;bool reject=false,nativeActions=false;
    options.localFileOpener=[&](const QString& file){opened.push_back(file);return !reject&&(!nativeActions||gui::openLocalFile(file));};
    options.localFileRevealer=[&](const QString& file){revealed.push_back(file);return !reject&&(!nativeActions||gui::revealLocalFile(file));};
    gui::MainWindow window(options);window.setWindowTitle(QStringLiteral("CipherChat 文件页验收（隔离账号）"));window.show();test::QtClientPage page(window);page.authenticate("password123",false);REQUIRE(wait([&]{return page.online();}));
    page.selectConversation("files_bob");auto* draft=widget<QPlainTextEdit>(window,"chatInput");draft->setPlainText(QStringLiteral("文件操作期间保留的草稿"));
    click(widget<QPushButton>(window,"filesNav"));auto* scroll=widget<QScrollArea>(window,"fileQueue");
    REQUIRE(wait([&]{return row(window,receivedId)!=nullptr;}));auto* done=row(window,receivedId);QPointer<QWidget> stable=done;
    scroll->ensureWidgetVisible(done);widget<QPushButton>(*done,"queueOpenFile")->setFocus();QTest::qWait(240);
    CHECK(stable==row(window,receivedId));CHECK(widget<QPushButton>(*done,"queueOpenFile")->hasFocus());
    CHECK_FALSE(widget<QProgressBar>(*done,"queueFileProgress")->isVisible());
    CHECK(widget<QLabel>(*done,"queueConversationLabel")->property("fullText").toString().contains("files_bob"));
    click(widget<QPushButton>(*done,"queueOpenFile"));click(widget<QPushButton>(*done,"queueOpenDirectory"));
    const auto actual=QFileInfo(QString::fromStdWString(received.wstring())).absoluteFilePath();CHECK(opened.back()==actual);CHECK(revealed.back()==actual);
    auto* cancelled=row(window,cancelledId);REQUIRE(cancelled);scroll->ensureWidgetVisible(cancelled);click(widget<QPushButton>(*cancelled,"queueOpenFile"));CHECK(opened.back()==QFileInfo(QString::fromStdWString(source.wstring())).absoluteFilePath());
    auto* missing=row(window,"missing-file-proof");REQUIRE(missing);CHECK_FALSE(widget<QPushButton>(*missing,"queueOpenFile")->isEnabled());CHECK_FALSE(widget<QPushButton>(*missing,"queueOpenDirectory")->isEnabled());
    auto* active=row(window,pendingId);REQUIRE(active);CHECK(widget<QProgressBar>(*active,"queueFileProgress")->isVisible());CHECK(widget<QProgressBar>(*active,"queueFileProgress")->width()<=110);
    auto* receiving=row(window,incomingId);REQUIRE(receiving);CHECK_FALSE(widget<QPushButton>(*receiving,"queueOpenFile")->isEnabled());CHECK_FALSE(widget<QPushButton>(*receiving,"queueOpenDirectory")->isEnabled());
    scroll->ensureWidgetVisible(done);click(widget<QPushButton>(*done,"queueConversation"));
    REQUIRE(wait([&]{return window.property("locatedFileId").toString()==QString::fromStdString(receivedId);}));
    CHECK(widget<QStackedWidget>(window,"messengerPages")->currentIndex()==0);CHECK(widget<QLineEdit>(window,"chatPeer")->text()=="files_bob");CHECK(draft->toPlainText()==QStringLiteral("文件操作期间保留的草稿"));
    auto* area=widget<QScrollArea>(window,"chatMessages");QWidget* located=nullptr;
    for(auto* item:area->findChildren<QWidget*>("fileCard")) if(item->property("fileId").toString()==QString::fromStdString(receivedId))located=item;
    REQUIRE(located);CHECK(located->property("locatedFile").toBool());CHECK(area->findChildren<QLabel*>("messageText").size()>100);
    CHECK(area->viewport()->rect().intersects(QRect(located->mapTo(area->viewport(),QPoint()),located->size())));shot(window,output/"located-old-file.png");
    click(widget<QPushButton>(window,"filesNav"));auto* filter=widget<QComboBox>(window,"fileFilter");filter->setCurrentIndex(1);QTest::qWait(80);CHECK(window.findChildren<QWidget*>("fileQueueRow").size()==2);CHECK(row(window,pendingId));filter->setCurrentIndex(2);QTest::qWait(80);CHECK_FALSE(row(window,pendingId));CHECK_FALSE(row(window,cancelledId));filter->setCurrentIndex(0);QTest::qWait(80);
    for(const auto size:{QSize(860,640),QSize(960,640),QSize(1366,768),QSize(1920,1080)}) for(bool dark:{false,true}) {
        window.setMinimumSize(size);window.resize(size);window.move(0,0);auto* appearance=widget<QPushButton>(window,"appearanceMode");if(appearance->isChecked()!=dark)appearance->click();scroll->verticalScrollBar()->setValue(0);QTest::qWait(100);
        CHECK(window.size()==size);CHECK(scroll->horizontalScrollBar()->maximum()==0);CHECK(filter->width()<=160);
        for(auto* item:window.findChildren<QWidget*>("fileQueueRow")) {CHECK(item->height()<=120);CHECK(item->width()<=1100);for(const auto* name:{"queueConversation","queueOpenFile","queueOpenDirectory"}) {auto* action=widget<QPushButton>(*item,name);CHECK(action->width()<230);CHECK(item->rect().contains(QRect(action->mapTo(item,QPoint()),action->size())));}}
        shot(window,output/("files-"+std::to_string(size.width())+(dark?"-dark.png":"-light.png")));
    }
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()/2);const int position=scroll->verticalScrollBar()->value();QTest::qWait(280);CHECK(scroll->verticalScrollBar()->value()==position);
    done=row(window,receivedId);scroll->ensureWidgetVisible(done);reject=true;click(widget<QPushButton>(*done,"queueOpenFile"));CHECK(widget<QLabel>(window,"notification")->text().contains(QStringLiteral("系统未能打开")));CHECK(draft->toPlainText()==QStringLiteral("文件操作期间保留的草稿"));reject=false;
    if(review) {
        nativeActions=true;scroll->ensureWidgetVisible(done);click(widget<QPushButton>(*done,"queueOpenFile"));click(widget<QPushButton>(*done,"queueOpenDirectory"));
        nlohmann::json ready={{"file",QString::fromStdWString(received.wstring()).toUtf8().toStdString()},{"profile",profiles.string()},{"opened",true},{"revealed",true}};std::ofstream(root/"native-ready.json")<<ready.dump(2);
        window.setMinimumSize(960,640);window.resize(960,640);window.move(0,0);
        QElapsedTimer timer;timer.start();while(timer.elapsed()<180000&&!std::filesystem::exists(root/"native-stop"))QTest::qWait(100);
    } else {
        REQUIRE(std::filesystem::remove(received));click(widget<QPushButton>(*done,"queueOpenFile"));CHECK(widget<QLabel>(window,"notification")->text().contains(QStringLiteral("已移动")));CHECK_FALSE(widget<QPushButton>(*row(window,receivedId),"queueOpenFile")->isEnabled());
        auto* again=widget<QPushButton>(*row(window,receivedId),"queueResume");REQUIRE(again->text()==QStringLiteral("重新下载"));click(again);
        REQUIRE(wait([&]{return std::filesystem::is_regular_file(received) && readBytes(received)==readBytes(source) && widget<QPushButton>(*row(window,receivedId),"queueOpenFile")->isEnabled();},15000));
        CHECK(draft->toPlainText()==QStringLiteral("文件操作期间保留的草稿"));shot(window,output/"cache-redownload.png");
    }
    for(const auto& id:{pendingId,incomingId}) {
        auto* item=row(window,id);REQUIRE(item);auto* cancel=widget<QPushButton>(*item,"queueCancel");
        if(cancel->isVisible()) {scroll->ensureWidgetVisible(item);click(cancel);REQUIRE(wait([&]{return !widget<QPushButton>(*row(window,id),"queueCancel")->isVisible();}));}
    }
    page.logout();
    nlohmann::json report={{"passed",true},{"scale",scale.toStdString()},{"real_tls_file_bytes_equal",true},{"historical_rows_are_fixture_records",true},{"exact_old_file_located",true},{"file_more_than_100_newer_records",true},{"draft_preserved",true},{"open_and_reveal_paths_correct",true},{"missing_file_disabled",true},{"progress_row_focus_stable",true},{"progress_bar_maximum_width",104},{"compact_actions",true}};
    std::ofstream(output/"result.json")<<report.dump(2);std::cout<<report.dump()<<'\n';
}
int main(int argc,char** argv) {QApplication app(argc,argv);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);return doctest::Context(argc,argv).run();}
