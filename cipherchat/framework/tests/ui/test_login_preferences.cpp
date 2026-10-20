#define DOCTEST_CONFIG_IMPLEMENT
#include "../acceptance/client_fixture.hpp"
#include "main_window.hpp"
#include "login_preferences.hpp"
#include "qt_client_page.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <fstream>
#include <iostream>
using namespace ccfw;
namespace {
QString qpath(const std::filesystem::path& path) { return QString::fromStdWString(path.wstring()); }
template<class T> T* control(QWidget& owner,const char* name) { auto* value=owner.findChild<T*>(name); REQUIRE(value); return value; }
bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<10000) { if(predicate()) return true; QTest::qWait(20); }
    return predicate();
}
QVariantMap config(const gui::Authenticate& request) {
    return {{"dataDirectory",qpath(request.dataDirectory)},{"host",QString::fromStdString(request.connection.host)},
        {"port",request.connection.port},{"serverName",QString::fromStdString(request.connection.expectedServerName)},
        {"caFile",qpath(request.connection.caFile)}};
}
void accepted(gui::LoginPreferences& preferences,const gui::Authenticate& request) {
    preferences.beginAuthentication(request);
    preferences.consume(gui::WorkerEvent{ClientEvent{AccountEvent{AccountPhase::ChallengePending}}});
    gui::RestoredView view; view.username=request.username; preferences.consume(gui::WorkerEvent{view});
}
QByteArray contents(const std::filesystem::path& path) { QFile file(qpath(path)); REQUIRE(file.open(QIODevice::ReadOnly)); return file.readAll(); }
void show(gui::MainWindow& window) { window.show(); window.raise(); window.activateWindow(); REQUIRE(QTest::qWaitForWindowActive(&window,5000)); }
void click(QWidget* target) { REQUIRE(target->isVisible()); QTest::mouseClick(target,Qt::LeftButton); }
}
TEST_CASE("Codex login preferences protect credentials and isolate accounts servers and profiles") {
    const auto output=test::tempDirectory("Codex-login-storage");
    gui::Authenticate request; request.dataDirectory=output/"profile";
    request.connection={"127.0.0.1",4433,output/"ca.pem","localhost",5000};
    std::ofstream(request.connection.caFile)<<"test-only-public-trust-A";
    request.username="remember_alice"; request.password="TestCredential123!";
    gui::LoginPreferences preferences; preferences.setContext(config(request));
    CHECK_FALSE(preferences.rememberAccount()); CHECK_FALSE(preferences.rememberPassword());
    preferences.setRememberPassword(true); CHECK(preferences.rememberAccount());
    preferences.beginAuthentication(request); CHECK(preferences.initialUsername({}).isEmpty());
    preferences.consume(gui::WorkerEvent{ClientEvent{Error{ErrorCode::AuthenticationFailed,"test","Rejected test login"}}});
    CHECK_FALSE(preferences.authenticating()); CHECK(preferences.initialUsername({}).isEmpty());
    accepted(preferences,request); CHECK(preferences.initialUsername({})=="remember_alice");
    CHECK(preferences.passwordFor("remember_alice")==QString::fromStdString(request.password));
    CHECK(preferences.passwordFor("other_user").isEmpty());
    CHECK(preferences.initialUsername("explicit_user")=="explicit_user");
    CHECK_FALSE(contents(request.dataDirectory/"preferences.ini").contains(QByteArray::fromStdString(request.password)));
    auto moved=request; moved.connection.caFile=output/"same-certificate-moved.pem";
    std::filesystem::copy_file(request.connection.caFile,moved.connection.caFile);
    preferences.setContext(config(moved)); CHECK(preferences.passwordFor("remember_alice")==QString::fromStdString(request.password));
    auto other=request; other.connection.port++;
    preferences.setContext(config(other)); CHECK(preferences.initialUsername({}).isEmpty()); CHECK(preferences.passwordFor("remember_alice").isEmpty());
    other=request; other.dataDirectory=output/"separate-profile";
    preferences.setContext(config(other)); CHECK_FALSE(preferences.rememberAccount()); CHECK(preferences.passwordFor("remember_alice").isEmpty());
    preferences.setContext(config(request)); preferences.setRememberPassword(false);
    CHECK(preferences.rememberAccount()); CHECK(preferences.initialUsername({})=="remember_alice"); CHECK(preferences.passwordFor("remember_alice").isEmpty());
    QSettings disk(qpath(request.dataDirectory/"preferences.ini"),QSettings::IniFormat); CHECK_FALSE(disk.contains("login/protectedPassword"));
    preferences.setRememberAccount(false); CHECK_FALSE(preferences.rememberPassword()); CHECK(preferences.initialUsername({}).isEmpty());
    disk.sync(); CHECK_FALSE(disk.contains("login/username"));
    preferences.setRememberPassword(true); accepted(preferences,request);
    disk.setValue("login/protectedPassword",QByteArray("not-a-protected-password")); disk.sync();
    gui::LoginPreferences damaged; damaged.setContext(config(request)); CHECK(damaged.passwordFor("remember_alice").isEmpty()); CHECK_FALSE(damaged.error().isEmpty());
    const auto blocked=output/"blocked"; std::filesystem::create_directories(blocked/"preferences.ini");
    other=request; other.dataDirectory=blocked;
    gui::LoginPreferences inaccessible; inaccessible.setContext(config(other)); inaccessible.setRememberAccount(true); CHECK_FALSE(inaccessible.error().isEmpty());
}
TEST_CASE("Codex real login remembers only successful credentials and forgets selected values") {
    const auto output=test::tempDirectory("Codex-login-runtime"); test::RealServer server;
    gui::WindowOptions options; options.connection=server.options(); options.dataDirectory=output/"client"; options.username="remember_alice";
    const auto credential=QStringLiteral("TestCredential123!");
    {
        gui::MainWindow window(options); show(window); test::QtClientPage page(window);
        auto* account=control<QCheckBox>(window,"rememberAccount"); auto* password=control<QCheckBox>(window,"rememberPassword");
        CHECK_FALSE(account->isChecked()); CHECK_FALSE(password->isChecked()); click(password); CHECK(account->isChecked()); CHECK(password->isChecked());
        page.authenticate(credential,true); REQUIRE(waitFor([&]{return page.online();}));
        CHECK_FALSE(contents(options.dataDirectory/"preferences.ini").contains(credential.toUtf8())); window.close();
    }
    options.username.clear(); QByteArray saved;
    {
        gui::MainWindow window(options); show(window); test::QtClientPage page(window);
        auto* user=control<QLineEdit>(window,"username"); auto* password=control<QLineEdit>(window,"password");
        CHECK(user->text()=="remember_alice"); CHECK(password->text()==credential); CHECK(password->echoMode()==QLineEdit::Password);
        CHECK_FALSE(page.online()); CHECK(control<QLabel>(window,"accountStatus")->property("phase").toString()=="idle");
        user->setText("other_user"); CHECK(password->text().isEmpty()); user->setText("remember_alice"); CHECK(password->text()==credential);
        auto* host=control<QLineEdit>(window,"serverHost"); const auto originalHost=host->text(); host->setText("other-server.invalid"); CHECK(password->text().isEmpty()); host->setText(originalHost); CHECK(password->text()==credential);
        QSettings disk(qpath(options.dataDirectory/"preferences.ini"),QSettings::IniFormat); saved=disk.value("login/protectedPassword").toByteArray(); REQUIRE_FALSE(saved.isEmpty());
        page.authenticate("WrongCredential000",false); REQUIRE(waitFor([&]{return control<QPushButton>(window,"loginButton")->isEnabled();}));
        CHECK_FALSE(page.online()); disk.sync(); CHECK(disk.value("login/protectedPassword").toByteArray()==saved); window.close();
    }
    {
        gui::MainWindow window(options); show(window); test::QtClientPage page(window);
        CHECK(control<QLineEdit>(window,"password")->text()==credential);
        // Recalled values alone do not submit; explicitly click Login.
        click(control<QPushButton>(window,"loginButton")); REQUIRE(waitFor([&]{return page.online();}));
        page.logout();
        click(control<QCheckBox>(window,"rememberPassword")); CHECK(control<QCheckBox>(window,"rememberAccount")->isChecked());
        QSettings disk(qpath(options.dataDirectory/"preferences.ini"),QSettings::IniFormat); CHECK_FALSE(disk.contains("login/protectedPassword")); window.close();
    }
    {
        gui::MainWindow window(options); show(window);
        CHECK(control<QLineEdit>(window,"username")->text()=="remember_alice"); CHECK(control<QLineEdit>(window,"password")->text().isEmpty());
        auto* account=control<QCheckBox>(window,"rememberAccount"); account->setFocus(); QTest::keyClick(account,Qt::Key_Space);
        CHECK_FALSE(account->isChecked()); CHECK_FALSE(control<QCheckBox>(window,"rememberPassword")->isChecked()); window.close();
    }
    {
        gui::MainWindow window(options); show(window);
        CHECK(control<QLineEdit>(window,"username")->text().isEmpty()); CHECK(control<QLineEdit>(window,"password")->text().isEmpty());
        CHECK_FALSE(control<QCheckBox>(window,"rememberAccount")->isChecked()); window.close();
    }
    std::cout<<"Codex login runtime evidence="<<output.string()<<'\n';
}
TEST_CASE("Codex remembered login controls remain usable at normal logical sizes") {
    const auto output=test::tempDirectory("Codex-login-layout");
    gui::WindowOptions options; options.dataDirectory=output/"client"; options.username="preview_account";
    gui::MainWindow window(options); show(window);
    click(control<QCheckBox>(window,"rememberPassword")); control<QLineEdit>(window,"password")->setText("MaskedPreviewCredential");
    for(const auto& size:{QSize(860,640),QSize(960,640),QSize(1366,768),QSize(1920,1080)}) for(bool dark:{false,true}) {
        window.setMinimumSize(size); window.resize(size); window.move(0,0);
        // Theme is configured for this rendering fixture; the theme entry is
        // intentionally hidden before authentication.
        auto* appearance=control<QPushButton>(window,"appearanceMode"); if(appearance->isChecked()!=dark) appearance->click();
        QTest::qWait(180); CHECK(window.size()==size);
        for (const auto* name : {"messagesNav", "contactsNav", "filesNav", "accountMenu", "mainMenu"}) CHECK_FALSE(control<QWidget>(window,name)->isVisible());
        for(const auto* name:{"username","password","rememberAccount","rememberPassword","loginButton"}) {
            auto* value=control<QWidget>(window,name); CHECK(value->isVisible());
            INFO(name<<" at "<<value->mapTo(&window,QPoint()).x()<<","<<value->mapTo(&window,QPoint()).y());
            CHECK(window.rect().contains(QRect(value->mapTo(&window,QPoint()),value->size())));
        }
        REQUIRE(window.grab().save(qpath(output/("codex-login-"+std::to_string(size.width())+(dark?"-dark.png":"-light.png")))));
    }
    std::cout<<"Codex login scale="<<qEnvironmentVariable("QT_SCALE_FACTOR").toStdString()<<" evidence="<<output.string()<<'\n'; window.close();
}
TEST_CASE("Codex separate registration validates confirmation and preserves login and failed account flows") {
    const auto output=test::tempDirectory("Codex-registration"); test::RealServer server;
    gui::WindowOptions options; options.connection=server.options(); options.dataDirectory=output/"client"; options.username="login_draft_user";
    gui::MainWindow window(options); show(window); test::QtClientPage page(window);
    auto* pages=control<QStackedWidget>(window,"sessionPages");
    auto* loginUser=control<QLineEdit>(window,"username"); auto* loginPassword=control<QLineEdit>(window,"password");
    loginPassword->setText("LoginDraftOnly123");
    CHECK(pages->currentIndex()==0); CHECK_FALSE(control<QWidget>(window,"registrationPage")->isVisible());
    click(control<QPushButton>(window,"registerButton")); CHECK(pages->currentIndex()==2);
    auto* user=control<QLineEdit>(window,"registrationUsername"); auto* password=control<QLineEdit>(window,"registrationPassword"); auto* confirmation=control<QLineEdit>(window,"registrationConfirmation");
    auto* submit=control<QPushButton>(window,"submitRegistration"); auto* back=control<QPushButton>(window,"backToLogin"); auto* feedback=control<QLabel>(window,"registrationFeedback");
    for(const auto* name:{"username","password","rememberAccount","rememberPassword","loginButton","registerButton","messagesNav","contactsNav","filesNav","mainMenu","accountMenu"}) CHECK_FALSE(control<QWidget>(window,name)->isVisible());
    CHECK(user->text().isEmpty()); CHECK(password->text().isEmpty()); CHECK(confirmation->text().isEmpty());
    CHECK(password->echoMode()==QLineEdit::Password); CHECK(confirmation->echoMode()==QLineEdit::Password);
    user->setText("signup_user"); password->setText("SignupCredential123!"); confirmation->setText("DifferentCredential123!");
    click(submit); CHECK(feedback->text().contains(QStringLiteral("不一致"))); CHECK(password->text()=="SignupCredential123!"); CHECK(confirmation->text()=="DifferentCredential123!"); CHECK(submit->isEnabled()); CHECK_FALSE(page.online());
    QTest::keyClick(confirmation,Qt::Key_Escape); CHECK(pages->currentIndex()==0);
    CHECK(password->text().isEmpty()); CHECK(confirmation->text().isEmpty()); CHECK(loginUser->text()=="login_draft_user"); CHECK(loginPassword->text()=="LoginDraftOnly123");
    click(control<QPushButton>(window,"registerButton")); CHECK(user->text()=="signup_user");
    user->setFocus(); QTest::keyClick(user,Qt::Key_Return); CHECK(password->hasFocus());
    password->setText("SignupCredential123!"); QTest::keyClick(password,Qt::Key_Return); CHECK(confirmation->hasFocus());
    confirmation->setText("SignupCredential123!"); QTest::keyClick(confirmation,Qt::Key_Return);
    CHECK_FALSE(submit->isEnabled()); CHECK_FALSE(back->isEnabled());
    REQUIRE(waitFor([&]{return page.online();})); CHECK(pages->currentIndex()==1); CHECK(loginUser->text()=="signup_user");
    CHECK(password->text().isEmpty()); CHECK(confirmation->text().isEmpty()); page.logout(); CHECK(pages->currentIndex()==0);
    CHECK(loginPassword->text().isEmpty());
    // Re-registering the actual existing TLS account is rejected and stays here.
    click(control<QPushButton>(window,"registerButton")); password->setText("SignupCredential123!"); confirmation->setText("SignupCredential123!"); click(submit);
    REQUIRE(waitFor([&]{return submit->isEnabled() && feedback->property("error").toBool();}));
    CHECK_FALSE(page.online()); CHECK(pages->currentIndex()==2); CHECK(user->text()=="signup_user"); CHECK_FALSE(feedback->text().isEmpty());
    REQUIRE(window.grab().save(qpath(output/"registration-rejected.png"))); click(back); CHECK(pages->currentIndex()==0);
    CHECK(loginUser->text()=="signup_user"); CHECK(loginPassword->text().isEmpty());
    page.authenticate("SignupCredential123!",false); REQUIRE(waitFor([&]{return page.online();})); page.logout();
    // A connection failure must also leave a recoverable registration page.
    auto* port=control<QSpinBox>(window,"serverPort"); const int originalPort=port->value(); port->setValue(1);
    click(control<QPushButton>(window,"registerButton")); user->setText("offline_signup_user"); password->setText("SignupCredential123!"); confirmation->setText("SignupCredential123!"); click(submit);
    REQUIRE(waitFor([&]{return submit->isEnabled() && feedback->property("error").toBool();})); CHECK(pages->currentIndex()==2); CHECK(user->text()=="offline_signup_user");
    port->setValue(originalPort); click(back); click(control<QPushButton>(window,"registerButton"));
    user->setText("preview_signup"); password->setText("MaskedPreviewCredential"); confirmation->setText("MaskedPreviewCredential");
    for(const auto& size:{QSize(860,640),QSize(960,640),QSize(1366,768),QSize(1920,1080)}) for(bool dark:{false,true}) {
        window.setMinimumSize(size); window.resize(size); window.move(0,0);
        auto* appearance=control<QPushButton>(window,"appearanceMode"); if(appearance->isChecked()!=dark) appearance->click(); QTest::qWait(180);
        CHECK(window.size()==size); CHECK(pages->currentIndex()==2);
        for(const auto* name:{"registrationCard","registrationUsername","registrationPassword","registrationConfirmation","submitRegistration","backToLogin"}) {
            auto* value=control<QWidget>(window,name); CHECK(value->isVisible()); CHECK(window.rect().contains(QRect(value->mapTo(&window,QPoint()),value->size())));
        }
        REQUIRE(window.grab().save(qpath(output/("codex-registration-"+std::to_string(size.width())+(dark?"-dark.png":"-light.png")))));
    }
    std::cout<<"Codex registration scale="<<qEnvironmentVariable("QT_SCALE_FACTOR").toStdString()<<" evidence="<<output.string()<<'\n';
    click(back); CHECK(loginUser->text()=="signup_user"); CHECK(password->text().isEmpty()); CHECK(confirmation->text().isEmpty()); window.close();
}
int main(int argc,char** argv) { QApplication app(argc,argv); return doctest::Context(argc,argv).run(); }
