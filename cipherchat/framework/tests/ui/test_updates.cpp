#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/update_fixture.hpp"
#include "update_controller.hpp"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QTest>
#include <QVBoxLayout>
#include <QDir>
#include <iostream>
int main(int argc,char** argv) {
    doctest::Context context; context.setAsDefaultForAssertsOutOfTestCases();
    QApplication app(argc,argv); ccfw::test::UpdateFixture f;
    auto extended=f.original;
    extended["new.bin"].clear();
    for(int block=0;block<48;++block) extended["new.bin"]+=std::string(ccfw::updates::blockSize,char(block));
    if (const auto feed=std::getenv("CCFW_UPDATE_FEED")) {
        f.latest=ccfw::updates::readJson(std::filesystem::path(feed)/"latest.json");
        _putenv_s("CIPHERCHAT_UPDATE_ROOT",feed);
    } else { f.latest=f.manifest("1.1",2,extended);f.publish(); }
    std::ifstream helper(CCFW_UPDATER_PATH, std::ios::binary);
    const std::string helperBytes{std::istreambuf_iterator<char>(helper), {}};
    if (helperBytes.empty()) return 6;
    ccfw::updates::writeAtomic(f.current / "cipherchat-updater.exe", helperBytes);
    QWidget window; auto* layout=new QVBoxLayout(&window); auto* entry=new QPushButton(QStringLiteral("软件更新")); layout->addWidget(entry);
    new ccfw::gui::UpdateController(&window,entry,QString::fromStdWString(f.current.wstring()),QString::fromUtf8(CCFW_UPDATER_PATH));
    window.show(); entry->click(); auto* dialog=window.findChild<QDialog*>("updateDialog");
    auto* check=dialog->findChild<QPushButton*>("checkUpdate"); auto* download=dialog->findChild<QPushButton*>("downloadUpdate"); auto* apply=dialog->findChild<QPushButton*>("applyUpdate");
    if(!check->isEnabled() || download->isEnabled() || apply->isEnabled()) return 1;
    check->click(); for(int i=0;i<500 && !download->isEnabled();++i) QTest::qWait(20);
    if(!download->isEnabled()) { std::cerr << dialog->findChild<QLabel*>("updateStatus")->text().toStdString(); return 2; }
    auto* pause=dialog->findChild<QPushButton*>("pauseUpdate");
    download->click();
    auto* process=window.findChild<QProcess*>();
    for(int i=0;i<500;++i) {
        QTest::qWait(5);
        const auto cache=f.current/".updates"/"cache";
        if(std::filesystem::is_directory(cache)&&std::filesystem::directory_iterator(cache)!=std::filesystem::directory_iterator{})break;
    }
    if(!pause->isVisible()||!pause->isEnabled()||apply->isEnabled())return 7;
    pause->click();for(int i=0;i<500&&!download->isEnabled();++i)QTest::qWait(10);
    if(!download->isEnabled()||download->text()!=QStringLiteral("继续下载")||apply->isEnabled()||process->state()!=QProcess::NotRunning)return 8;
    download->click(); for(int i=0;i<500 && !apply->isEnabled();++i) QTest::qWait(20);
    if(!apply->isEnabled()) return 3;
    if(ccfw::updates::versionPath(f.current)!=".") return 4;
    const auto output=qEnvironmentVariable("CCFW_UPDATE_SCREENSHOT"); if(!output.isEmpty() && !dialog->grab().save(output)) return 5;
    dialog->close(); window.close();
    std::cout << "Real helper TLS download, verified-cache pause/resume, staged activation gate and nonblocking UI passed.\n";
    return 0;
}
