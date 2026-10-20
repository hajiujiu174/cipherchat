#include "services/mockservice.h"
#include "ui/accountdialog.h"
#include "ui/theme.h"
#include "gui/update_controller.hpp"
#include "update/update.hpp"
#include "server/net/tls_server.hpp"
#include <QtTest>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <fstream>
#include <future>
#include <map>
namespace fs=std::filesystem;
using namespace ccfw::updates;
struct Feed {
    static fs::path directory(const fs::path& p){fs::create_directories(p);return p;}
    QTemporaryDir temp;
    fs::path root=fs::path(temp.path().toStdWString()),installed=root/"installed",feed=root/"feed",serverDir=directory(root/"server");
    cc::server::Storage db{(serverDir/"server.db").string()};cc::server::Router router{db};
    cc::server::TlsServer tls{router,(serverDir/"cert.pem").string(),(serverDir/"key.pem").string()};std::future<bool> worker;
    Json old,latest;std::map<std::string,std::string> files;
    Feed(){
        worker=std::async(std::launch::async,[this]{return tls.listen(0,"127.0.0.1");});if(!tls.waitReady(3000))throw std::runtime_error("server unavailable");
        fs::create_directories(installed);fs::create_directories(feed/"messager/blobs");qputenv("CIPHERCHAT_UPDATE_ROOT",QByteArray::fromStdString(feed.string()));
        auto read=[](const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
        files={{"Messager.exe",std::string(blockSize,'a')},{"cipherchat-updater.exe",read(fs::u8path(UPDATER_PATH))},{"server-cert.pem",read(serverDir/"cert.pem")},
            {"update-config.json",Json{{"host","127.0.0.1"},{"port",tls.port()},{"server_name","localhost"},{"channel","messager"}}.dump()}};
        old=manifest("1.0",1,files);for(const auto& [name,data]:files)writeAtomic(installed/name,data);writeAtomic(installed/"release.json",old.dump());writeAtomic(installed/"cipherchat-launcher.exe","fixture launcher");writeAtomic(installed/"account.db","existing private account fixture");
        auto next=files;next["Messager.exe"]=std::string(blockSize*3,'b');latest=manifest("1.1",2,next);publish();
    }
    ~Feed(){tls.stop();if(worker.valid())worker.wait();qunsetenv("CIPHERCHAT_UPDATE_ROOT");}
    Json manifest(const std::string& ver,std::uint64_t seq,const std::map<std::string,std::string>& content){
        Json m={{"format",1},{"platform","windows-x64"},{"block_size",blockSize},{"product","messager"},{"version",ver},{"sequence",seq},{"notes","登录前即可更新，账号和聊天记录保持。"},{"files",Json::array()}};
        for(const auto& [name,data]:content){Json f={{"path",name},{"size",data.size()},{"blocks",Json::array()}};for(std::size_t n=0;n<data.size();n+=blockSize){auto value=data.substr(n,blockSize);auto h=digest(value);writeAtomic(feed/"messager/blobs"/h,value);f["blocks"].push_back({{"size",value.size()},{"sha256",h}});}m["files"].push_back(f);}return m;
    }
    void publish(){writeAtomic(feed/"messager/latest.json",latest.dump());}
};
class UpdateTest:public QObject {
    Q_OBJECT
    bool until(const std::function<bool()>& f,int timeout=15000){QElapsedTimer t;t.start();while(t.elapsed()<timeout){if(f())return true;QTest::qWait(20);}return f();}
  private slots:
    void initTestCase(){Messager::Theme::instance().apply();Messager::Theme::instance().setSkin(qEnvironmentVariable("MESSAGER_TEST_SKIN")=="pencil"?Messager::Theme::PencilSkin:Messager::Theme::DefaultSkin);}
    void productAndChannelIsolation(){
        Feed f;validate(f.old);auto legacy=f.latest;legacy.erase("product");QVERIFY_EXCEPTION_THROWN(validate(legacy),std::exception);
        auto wrong=f.latest;wrong["product"]="cipherchat";for(auto& file:wrong["files"])if(file["path"]=="Messager.exe")file["path"]="cipherchat.exe";validate(wrong);
        QVERIFY_EXCEPTION_THROWN(check(f.installed,[&](const std::string&){return wrong.dump();}),std::exception);
        auto read=remoteFetcher(f.installed);QCOMPARE(check(f.installed,read)["version"].get<std::string>(),std::string("1.1"));
    }
    void loginAutomaticallyPromptsAndStages(){
        Feed f;Messager::MockService service;Messager::AccountDialog login(&service);auto* entry=login.findChild<QPushButton*>("loginUpdateButton");QVERIFY(entry);
        ccfw::gui::UpdateController updates(&login,entry,QString::fromStdWString(f.installed.wstring()),{},true);
        auto* label=login.findChild<QLabel*>("loginUpdateStatus");connect(&updates,&ccfw::gui::UpdateController::statusChanged,label,[label](const QString& s){label->setText(s.section('\n',0,0));});
        login.show();auto* dialog=login.findChild<QDialog*>("updateDialog");QVERIFY(dialog);
        auto* download=dialog->findChild<QPushButton*>("downloadUpdate");auto* apply=dialog->findChild<QPushButton*>("applyUpdate");
        QVERIFY(until([&]{return dialog->isVisible()&&download->isEnabled();}));QVERIFY(login.findChild<QPushButton*>("accountSubmit")->isEnabled());QVERIFY(!fs::exists(f.installed/".updates/versions/1.1"));
        const auto output=qEnvironmentVariable("MESSAGER_EVIDENCE_DIR");QDir().mkpath(output);
        Messager::Theme::instance().setMode(Messager::Theme::Light);QTest::qWait(50);if(!output.isEmpty()){QVERIFY(login.grab().save(output+"/login-update-light.png"));QVERIFY(dialog->grab().save(output+"/update-prompt-light.png"));}
        Messager::Theme::instance().setMode(Messager::Theme::Dark);QTest::qWait(50);if(!output.isEmpty())QVERIFY(dialog->grab().save(output+"/update-prompt-dark.png"));
        QTest::mouseClick(download,Qt::LeftButton);QVERIFY(until([&]{return apply->isEnabled();},30000));QCOMPARE(versionPath(f.installed),std::string("."));
        if(!output.isEmpty())QVERIFY(dialog->grab().save(output+"/update-staged-dark.png"));
        activate(f.installed,"1.1");QCOMPARE(versionPath(f.installed),std::string(".updates/versions/1.1"));QCOMPARE(fs::file_size(f.installed/versionPath(f.installed)/"Messager.exe"),std::uintmax_t(blockSize*3));rollback(f.installed);QCOMPARE(versionPath(f.installed),std::string("."));
        QFile account(QString::fromStdWString((f.installed/"account.db").wstring()));QVERIFY(account.open(QIODevice::ReadOnly));QCOMPARE(account.readAll(),QByteArray("existing private account fixture"));
        dialog->close();Messager::Theme::instance().setMode(Messager::Theme::Light);
    }
    void currentVersionDoesNotPrompt(){
        Feed f;f.latest=f.old;f.publish();Messager::MockService service;Messager::AccountDialog login(&service);ccfw::gui::UpdateController updates(&login,login.findChild<QPushButton*>("loginUpdateButton"),QString::fromStdWString(f.installed.wstring()),{},true);login.show();
        auto* d=login.findChild<QDialog*>("updateDialog");QVERIFY(until([&]{return d->findChild<QLabel*>("updateStatus")->text().contains("已经是最新版本")&&d->findChild<QPushButton*>("checkUpdate")->isEnabled();}));QVERIFY(!d->isVisible());QVERIFY(login.findChild<QPushButton*>("accountSubmit")->isEnabled());
    }
    void failedCheckLeavesLoginUsable(){
        Feed f;fs::remove(f.feed/"messager/latest.json");Messager::MockService service;Messager::AccountDialog login(&service);ccfw::gui::UpdateController updates(&login,login.findChild<QPushButton*>("loginUpdateButton"),QString::fromStdWString(f.installed.wstring()),{},true);login.show();
        auto* d=login.findChild<QDialog*>("updateDialog");QVERIFY(until([&]{return d->findChild<QLabel*>("updateStatus")->text().contains("更新资源不存在")&&d->findChild<QPushButton*>("checkUpdate")->isEnabled();}));QVERIFY(!d->isVisible());QVERIFY(login.findChild<QPushButton*>("accountSubmit")->isEnabled());
    }
    void fullReleaseActivatesAndLaunches(){
        const auto published=qEnvironmentVariable("MESSAGER_RELEASE_FEED"),portable=qEnvironmentVariable("MESSAGER_RELEASE_RUNTIME");
        if(published.isEmpty()||portable.isEmpty())QSKIP("Full release paths are required for this delivery test");
        Feed f;auto source=fs::path(portable.toStdWString());
        const auto publishedManifest=readJson(fs::path(published.toStdWString())/"messager/latest.json");const auto publishedVersion=publishedManifest.at("version").get<std::string>();
        for(const auto& p:fs::recursive_directory_iterator(source))if(p.is_regular_file()){
            auto relative=fs::relative(p.path(),source);if(relative=="release.json")continue;
            fs::create_directories((f.installed/relative).parent_path());fs::copy_file(p.path(),f.installed/relative,fs::copy_options::overwrite_existing);
        }
        // A deliberately older installed manifest uses the same full runtime to exercise reuse.
        std::map<std::string,std::string> base=f.files;
        base["update-config.json"]=Json{{"host","127.0.0.1"},{"port",f.tls.port()},{"server_name","localhost"},{"channel","messager"}}.dump();
        for(const auto& [name,data]:base)writeAtomic(f.installed/name,data);
        f.old=f.manifest("0.3.10",publishedManifest.at("sequence").get<std::uint64_t>()-1,base);writeAtomic(f.installed/"release.json",f.old.dump());
        qputenv("CIPHERCHAT_UPDATE_ROOT",published.toUtf8());
        auto staged=stage(f.installed,remoteFetcher(f.installed),{}, {downloadConnections});QCOMPARE(staged["version"].get<std::string>(),publishedVersion);
        const auto capture=QString::fromStdWString((f.root/"restarted-capture").wstring());
        QProcess prior;prior.start(qEnvironmentVariable("WINDIR")+"/System32/ping.exe",{"-n","3","127.0.0.1"});QVERIFY(prior.waitForStarted(5000));const auto pid=prior.processId();
        writeAtomic(f.installed/".updates"/("restart-"+std::to_string(pid)+".json"),Json::array({"--capture",capture.toStdString()}).dump());
        QProcess helper;helper.start(QString::fromStdWString((f.installed/"cipherchat-updater.exe").wstring()),{"--activate",QString::fromStdWString(f.installed.wstring()),QString::fromStdString(publishedVersion),"--wait-pid",QString::number(pid)});QVERIFY(helper.waitForStarted(5000));QTest::qWait(100);QCOMPARE(versionPath(f.installed),std::string("."));
        QVERIFY(until([&]{return helper.state()==QProcess::NotRunning;},30000));QVERIFY2(helper.exitCode()==0,helper.readAllStandardOutput().constData());
        const auto active=f.installed/versionPath(f.installed);QCOMPARE(versionPath(f.installed),".updates/versions/"+publishedVersion);QVERIFY(fs::is_regular_file(active/"Messager.exe"));
        QVERIFY(until([&]{return QFile::exists(capture+"/capture-check.json");},25000));QTest::qWait(500);
        auto output=qEnvironmentVariable("MESSAGER_EVIDENCE_DIR");if(!output.isEmpty()){QFile::copy(capture+"/capture-check.json",output+"/update-restarted-capture.json");QFile::copy(capture+"/01-chat-light.png",output+"/update-restarted-light.png");}
        rollback(f.installed);QCOMPARE(versionPath(f.installed),std::string("."));QVERIFY(fs::is_regular_file(f.installed/"account.db"));
    }
};
QTEST_MAIN(UpdateTest)
#include "tst_updates.moc"
