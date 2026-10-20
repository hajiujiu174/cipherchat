#include "server/net/tls_server.hpp"
#include "services/cipherchatservice.h"
#include "services/loginpreferences.h"
#include "ui/accountdialog.h"
#include "ui/groupmemberpicker.h"
#include "ui/mainwindow.h"
#include "ui/profilepage.h"
#include "ui/miniappspage.h"
#include "ui/gomokupage.h"
#include "services/gomokuwire.h"
#include "ccfw/local_store.hpp"
#include "ccfw/session_service.hpp"
#include "../../cipherchat/framework/src/snapshot_codec.hpp"
#include <sqlite3.h>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QPainter>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QListView>
#include <QBuffer>
#include <QScrollBar>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QUuid>
#include <QtTest>
#include <future>
#include <mutex>
#include <map>
using namespace Messager;
class LocalServer {
  public:
    std::filesystem::path path;
    cc::server::Storage storage;
    cc::server::Router router;
    cc::server::TlsServer tls;
    std::future<bool> thread;
    std::mutex mailMutex;
    std::map<std::string,std::string> mailCodes;
    std::string code(const std::string& email) { std::lock_guard lock(mailMutex); return mailCodes[email]; }
    explicit LocalServer(const QString& directory)
        : path(directory.toStdWString()), storage((path / "server.db").string()), router(storage),
          tls(router, (path / "cert.pem").string(), (path / "key.pem").string()) {
        router.setFileCacheEnabled(false);
        router.requireRegistrationEmail(std::make_shared<cc::server::RegistrationEmail>([this](const auto& email, const auto& code) {
            std::lock_guard lock(mailMutex); mailCodes[email] = code; return std::string{};
        }));
        router.enablePasswordReset(std::make_shared<cc::server::RegistrationEmail>([this](const auto& email, const auto& code) {
            std::lock_guard lock(mailMutex); mailCodes["reset:"+email]=code; return std::string{};
        }));
        router.enableEmailLogin(std::make_shared<cc::server::RegistrationEmail>([this](const auto& email,const auto& code){std::lock_guard lock(mailMutex);mailCodes["login:"+email]=code;return std::string{};}));
        thread = std::async(std::launch::async, [this] { return tls.listen(0, "127.0.0.1"); });
        if (!tls.waitReady(3000))
            qFatal("Loopback test server failed to start");
    }
    ~LocalServer() {
        tls.stop();
        if (thread.valid())
            thread.wait();
    }
};
class LiveTest : public QObject {
    Q_OBJECT
    QString root, evidence;
    std::unique_ptr<LocalServer> server;
    QJsonObject config(const QString& user) {
        return {{"host", "127.0.0.1"},
                {"port", int(server->tls.port())},
                {"serverName", "localhost"},
                {"caFile", root + "/server/cert.pem"},
                {"dataDirectory", root + "/" + user},
                {"username", user},
                {"directEnabled", false}};
    }
    bool waitUntil(const std::function<bool()>& predicate, int timeout = 15000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeout) {
            if (predicate())
                return true;
            QTest::qWait(25);
        }
        return predicate();
    }
    void cancelConnectionSettings(AccountDialog* dialog) {
        QTest::mouseClick(dialog->findChild<QPushButton*>("connectionSettingsButton"), Qt::LeftButton);
        auto* settings = dialog->findChild<QDialog*>("connectionDialog");
        QVERIFY(settings && settings->isVisible());
        QVERIFY(settings->findChild<QLineEdit*>("connection_accountScope")->text().isEmpty());
        auto* buttons = settings->findChild<QDialogButtonBox*>();
        QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        QVERIFY(!settings->isVisible());
    }
    void signUp(MainWindow& window) {
        window.showLogin();
        auto* dialog = window.accountDialog();
        QVERIFY(dialog);
        cancelConnectionSettings(dialog);
        dialog->grab().save(evidence + "/live-account.png");
        QTest::mouseClick(dialog->findChild<QPushButton*>("registerTab"), Qt::LeftButton);
        // Synchronous validation must leave the form usable for the next attempt.
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"), Qt::LeftButton);
        QVERIFY(dialog->findChild<QPushButton*>("accountSubmit")->isEnabled());
        QVERIFY(!dialog->findChild<QLabel*>("accountError")->text().isEmpty());
        const auto email = dialog->findChild<QLineEdit*>("accountUsername")->text() + "@example.test";
        dialog->findChild<QLineEdit*>("registrationEmail")->setText(email);
        QTest::mouseClick(dialog->findChild<QPushButton*>("sendRegistrationCode"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return dialog->findChild<QLabel*>("registrationCodeStatus")->isVisible(); }));
        QVERIFY(!dialog->findChild<QPushButton*>("sendRegistrationCode")->isEnabled());
        dialog->findChild<QLineEdit*>("registrationEmailCode")->setText(QString::fromStdString(server->code(email.toStdString())));
        dialog->findChild<QLineEdit*>("accountPassword")->setText("MessagerTest-2026!");
        dialog->findChild<QLineEdit*>("accountConfirmation")->setText("MessagerTest-2026!");
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"), Qt::LeftButton);
    }
    void compareAndApprove(MainWindow& window, const QString& peer, const QJsonObject& expected, int& count) {
        Q_UNUSED(expected)
        Q_UNUSED(count)
        QVERIFY(window.findChild<QDialog*>("identityDialog_"+peer)==nullptr);
    }
    static int textCount(const CipherChatService& service, const QString& id, const QString& text) {
        int n = 0;
        for (const auto& m : service.messages(id))
            n += m.text == text;
        return n;
    }
    static QPushButton* visibleButton(MainWindow& window, const QString& name) {
        for (auto* button : window.findChildren<QPushButton*>(name))
            if (button->isVisible())
                return button;
        return nullptr;
    }
    void saveJson(const QString& name, const QJsonObject& o) {
        QFile file(evidence + "/" + name);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(o).toJson());
    }
  private slots:
    void emailCodeLoginHasSeparatePageAndRejectsReplay() {
        CipherChatService original(config("otp_login"));AppController oc(&original);MainWindow ow(&oc);signUp(ow);
        QVERIFY(waitUntil([&]{return original.online();}));original.disconnectAccount();QVERIFY(waitUntil([&]{return !original.online();}));
        CipherChatService service(config("otp_login"));AppController controller(&service);MainWindow window(&controller);window.showLogin();
        auto* parent=window.accountDialog();QVERIFY(parent);QTest::mouseClick(parent->findChild<QPushButton*>("emailLoginEntry"),Qt::LeftButton);
        auto* page=parent->findChild<QDialog*>("emailLoginDialog");QVERIFY(page&&page->isVisible());QVERIFY(!parent->isVisible());
        auto* email=page->findChild<QLineEdit*>("emailLoginEmail");auto* code=page->findChild<QLineEdit*>("emailLoginCode");
        auto* send=page->findChild<QPushButton*>("emailLoginSendCode");auto* submit=page->findChild<QPushButton*>("emailLoginSubmit");auto* status=page->findChild<QLabel*>("emailLoginStatus");
        QVERIFY(!page->findChild<QLineEdit*>("accountPassword"));email->setText("invalid");QTest::mouseClick(send,Qt::LeftButton);QVERIFY(status->text().contains("有效"));
        email->setText("otp_login@example.test");QTest::mouseClick(send,Qt::LeftButton);QVERIFY(waitUntil([&]{return status->text().contains("验证码已发送");}));
        QVERIFY(!send->isEnabled());const auto actual=QString::fromStdString(server->code("login:otp_login@example.test"));QCOMPARE(actual.size(),6);
        QDir().mkpath(evidence+"/email-login");Theme::instance().setMode(Theme::Light);QTest::qWait(60);page->grab().save(evidence+"/email-login/email-login-light.png");
        Theme::instance().setMode(Theme::Dark);QTest::qWait(60);page->grab().save(evidence+"/email-login/email-login-dark.png");Theme::instance().setMode(Theme::Light);
        code->setText(actual=="000000"?"111111":"000000");QTest::mouseClick(submit,Qt::LeftButton);
        QVERIFY(waitUntil([&]{return submit->isEnabled()&&status->text().contains("验证码");}));QVERIFY(!service.online());
        code->setText(actual);QTest::mouseClick(submit,Qt::LeftButton);QVERIFY(waitUntil([&]{return service.online();}));QCOMPARE(service.username(),QString("otp_login"));QVERIFY(service.ownIdentity().isEmpty());
        service.disconnectAccount();QVERIFY(waitUntil([&]{return !service.online();}));
        auto replay=config("otp_login");replay["username"]="otp_login@example.test";replay["email"]="otp_login@example.test";replay["emailCode"]=actual;replay["emailLogin"]=true;
        CipherChatService repeated(replay);QSignalSpy errors(&repeated,&ChatService::operationError);repeated.authenticate(replay);QVERIFY(waitUntil([&]{return !errors.isEmpty();}));QVERIFY(!repeated.online());
    }
    void loginCodeExpiresAndIsThrottled() {
        std::int64_t now=100000;std::string delivered;
        cc::server::RegistrationEmail verifier([&](const auto&,const auto& code){delivered=code;return std::string{};},[&]{return now;});
        QVERIFY(verifier.request("otp_fixture","otp_fixture@example.test").ok);QCOMPARE(delivered.size(),std::size_t(6));
        const auto throttled=verifier.request("otp_fixture","otp_fixture@example.test");QVERIFY(!throttled.ok);QVERIFY(throttled.retryAfter>0);
        now+=600001;QVERIFY(!verifier.consume("otp_fixture","otp_fixture@example.test",delivered).ok);
        QVERIFY(verifier.request("otp_fixture","otp_fixture@example.test").ok);QVERIFY(verifier.consume("otp_fixture","otp_fixture@example.test",delivered).ok);
        QVERIFY(!verifier.consume("otp_fixture","otp_fixture@example.test",delivered).ok);
    }
    void ordinaryStorageAndLegacyImport() {
        struct LegacyReader final : ccfw::ISecretProtector {
            int seals=0,reads=0;
            ccfw::Result<cc::Bytes> seal(cc::BytesView) override { ++seals; return ccfw::Result<cc::Bytes>::failure({ccfw::ErrorCode::NotImplemented,"test","no new secrets"}); }
            ccfw::Result<cc::Bytes> unseal(cc::BytesView b) override { ++reads; if(!b.starts_with("LEGACY:"))return ccfw::Result<cc::Bytes>::failure({ccfw::ErrorCode::StorageFailure,"test","bad legacy fixture"});return ccfw::Result<cc::Bytes>::success(cc::Bytes(b.substr(7))); }
        } reader;
        const auto file=std::filesystem::path((root+"/ordinary-storage.db").toStdWString());
        const ccfw::AccountScope scope{"ordinary-test","ordinary_user"};
        ccfw::SqliteLocalStore store(reader);
        QVERIFY(store.open(file,scope));
        ccfw::SessionService messages(store);
        auto sent=messages.encryptAndCommit({"plain-1","","ordinary_user","friend","chat","ordinary visible message",0,123});
        QVERIFY(sent);cc::Bytes raw;QVERIFY(cc::base64Decode(sent.value().payloadBase64,raw));
        const auto payload=nlohmann::json::parse(raw);
        QCOMPARE(payload.at("chat_mode").get<std::string>(),std::string("ordinary-v1"));
        cc::Bytes body;QVERIFY(cc::base64Decode(payload.at("body_base64").get<std::string>(),body));
        QCOMPARE(body,std::string("ordinary visible message"));
        auto clean=store.load();QVERIFY(clean);QVERIFY(!clean.value().identity);QVERIFY(clean.value().sessions.empty());QVERIFY(clean.value().trustedPeers.empty());
        QCOMPARE(reader.seals,0);QCOMPARE(reader.reads,0);
        auto legacy=nlohmann::json(clean.value());legacy["format"]=1;legacy["identity"]={{"private","archived-key-sentinel"}};
        const auto cbor=nlohmann::json::to_cbor(legacy);cc::Bytes old="LEGACY:";old.append(reinterpret_cast<const char*>(cbor.data()),cbor.size());
        store.close();sqlite3* db=nullptr;QCOMPARE(sqlite3_open(file.string().c_str(),&db),SQLITE_OK);
        sqlite3_stmt* query=nullptr;QCOMPARE(sqlite3_prepare_v2(db,"UPDATE client_state SET protected_snapshot=?",-1,&query,nullptr),SQLITE_OK);
        sqlite3_bind_blob(query,1,old.data(),int(old.size()),SQLITE_TRANSIENT);QCOMPARE(sqlite3_step(query),SQLITE_DONE);sqlite3_finalize(query);sqlite3_close(db);
        QVERIFY(store.open(file,scope));auto imported=store.load();QVERIFY(imported);QCOMPARE(imported.value().history.size(),std::size_t(1));
        QCOMPARE(imported.value().history.front().text,std::string("ordinary visible message"));QVERIFY(!imported.value().identity);QVERIFY(imported.value().outbox.empty());
        QVERIFY(std::filesystem::exists(file.string()+".before-ordinary.bak"));QCOMPARE(reader.seals,0);QVERIFY(reader.reads>0);
        const auto persisted=nlohmann::json(imported.value()).dump();QVERIFY(persisted.find("archived-key-sentinel")==std::string::npos);QVERIFY(persisted.find("\"identity\"")==std::string::npos);
        store.close();
    }
    void loginFromFreshDeviceWithoutPrivateKeys() {
        CipherChatService first(config("ordinary_newdevice"));AppController controller(&first);MainWindow window(&controller);
        signUp(window);QVERIFY(waitUntil([&]{return first.online();}));QVERIFY(first.ownIdentity().isEmpty());
        const auto account=server->storage.findAccount("ordinary_newdevice");QVERIFY(account);QVERIFY(account->signPub.empty());QVERIFY(account->dhPub.empty());QVERIFY(account->signedPrekey.empty());
        first.disconnectAccount();QVERIFY(waitUntil([&]{return !first.online();}));
        auto connection=config("ordinary_newdevice");connection["dataDirectory"]=root+"/brand-new-device";
        connection["password"]="Incorrect-password!";
        CipherChatService fresh(connection);QSignalSpy errors(&fresh,&ChatService::operationError);
        fresh.authenticate(connection);QVERIFY(waitUntil([&]{return !errors.isEmpty();}));QVERIFY(!fresh.online());
        connection["password"]="MessagerTest-2026!";fresh.authenticate(connection);
        QVERIFY(waitUntil([&]{return fresh.online();}));QVERIFY(fresh.ownIdentity().isEmpty());
        fresh.disconnectAccount();
    }
    void usernameOrEmailLoginKeepsOriginalHistory() {
        CipherChatService owner(config("ordinary_alias")),peer(config("ordinary_alias_friend"));
        AppController ac(&owner),bc(&peer);MainWindow aw(&ac),bw(&bc);
        signUp(aw);signUp(bw);QVERIFY(waitUntil([&]{return owner.online()&&peer.online();}));
        const QString conversation=owner.openDirectConversation("ordinary_alias_friend");
        owner.send(conversation,"history retained across email login");
        QVERIFY(waitUntil([&]{return textCount(peer,"peer:ordinary_alias","history retained across email login")==1;}));
        owner.disconnectAccount();QVERIFY(waitUntil([&]{return !owner.online();}));
        auto connection=config("ordinary_alias");connection["username"]="ORDINARY_ALIAS@EXAMPLE.TEST";connection["password"]="MessagerTest-2026!";
        CipherChatService email(connection);AppController ec(&email);MainWindow ew(&ec);
        ew.showLogin();auto* dialog=ew.accountDialog();QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QLabel*>("accountUsernameCaption")->text(),QString("用户名或邮箱"));
        QCOMPARE(dialog->findChild<QLineEdit*>("accountUsername")->placeholderText(),QString("请输入用户名或已绑定邮箱"));
        dialog->findChild<QLineEdit*>("accountPassword")->setText("Wrong-email-password!");QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"),Qt::LeftButton);
        QVERIFY(waitUntil([&]{return dialog->findChild<QLabel*>("accountError")->isVisible();}));QVERIFY(!email.online());
        dialog->findChild<QLineEdit*>("accountPassword")->setText("MessagerTest-2026!");QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"),Qt::LeftButton);
        QVERIFY(waitUntil([&]{return email.online();}));QCOMPARE(email.username(),QString("ordinary_alias"));
        QCOMPARE(textCount(email,"peer:ordinary_alias_friend","history retained across email login"),1);
        email.send(email.openDirectConversation("ordinary_alias_friend"),"sent after email login");
        QVERIFY(waitUntil([&]{return textCount(peer,"peer:ordinary_alias","sent after email login")==1;}));
        QVERIFY(email.ownIdentity().isEmpty());
        email.disconnectAccount();peer.disconnectAccount();
        auto absent=connection;absent["username"]="not-bound@example.test";
        CipherChatService unknown(absent);QSignalSpy errors(&unknown,&ChatService::operationError);unknown.authenticate(absent);
        QVERIFY(waitUntil([&]{return !errors.isEmpty();}));QVERIFY(!unknown.online());
    }
    void initTestCase() {
        QCoreApplication::setOrganizationName("MessagerTests");
        QCoreApplication::setApplicationName("LiveAuth");
        root = QDir::currentPath() + "/live-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(root + "/server");
        evidence = qEnvironmentVariable("MESSAGER_EVIDENCE_DIR", root + "/evidence");
        QDir().mkpath(evidence);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
        qApp->setStyle(QStyleFactory::create("Fusion"));
        qApp->setQuitOnLastWindowClosed(false);
        Theme::instance().apply();
        Theme::instance().setSkin(qEnvironmentVariable("MESSAGER_TEST_SKIN")=="pencil"?Theme::PencilSkin:Theme::DefaultSkin);
        server = std::make_unique<LocalServer>(root + "/server");
    }
    void accountEntryLayout() {
        Theme::instance().setMode(Theme::Light);
        CipherChatService service(config("layout_fixture"));
        AppController controller(&service);
        MainWindow window(&controller);
        window.showLogin();
        QTest::qWait(100);
        auto* dialog = window.accountDialog();
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(!window.isVisible());
        QCOMPARE(dialog->width(), 530);
        QVERIFY(dialog->parentWidget() == nullptr);
        QVERIFY(!dialog->findChild<QWidget*>("connectionDialog"));
        QVERIFY(!dialog->findChild<QLineEdit*>("accountConfirmation")->isVisible());
        QVERIFY(!dialog->findChild<QCheckBox*>("rememberUsername")->isChecked());
        QVERIFY(!dialog->findChild<QCheckBox*>("rememberPassword")->isChecked());
        dialog->grab().save(evidence + "/01-login-reference.png");
        QTest::mouseClick(dialog->findChild<QPushButton*>("registerTab"), Qt::LeftButton);
        QTest::qWait(50);
        QVERIFY(dialog->findChild<QLineEdit*>("accountConfirmation")->isVisible());
        QVERIFY(!window.isVisible());
        dialog->grab().save(evidence + "/02-register-reference.png");
        dialog->findChild<QLineEdit*>("accountPassword")->setText("fixture-one");
        dialog->findChild<QLineEdit*>("accountConfirmation")->setText("fixture-two");
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"), Qt::LeftButton);
        QVERIFY(dialog->findChild<QLabel*>("accountError")->text().contains("不一致"));
        QVERIFY(!window.isVisible());
        QTest::mouseClick(dialog->findChild<QPushButton*>("registerTab"), Qt::LeftButton);
        Theme::instance().setMode(Theme::Dark);
        QTest::qWait(50);
        dialog->grab().save(evidence + "/03-login-dark.png");
        Theme::instance().setMode(Theme::Light);
        dialog->reject();
    }
    void registrationEmailRejectsWrongCode() {
        CipherChatService service(config("email_fixture"));
        auto* dialog = new AccountDialog(&service);
        dialog->show();
        QTest::mouseClick(dialog->findChild<QPushButton*>("registerTab"), Qt::LeftButton);
        auto* email = dialog->findChild<QLineEdit*>("registrationEmail");
        email->setText("invalid");
        QTest::mouseClick(dialog->findChild<QPushButton*>("sendRegistrationCode"), Qt::LeftButton);
        QVERIFY(dialog->findChild<QLabel*>("accountError")->text().contains("邮箱"));
        email->setText("email_fixture@example.test");
        QTest::mouseClick(dialog->findChild<QPushButton*>("sendRegistrationCode"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return dialog->findChild<QLabel*>("registrationCodeStatus")->isVisible(); }));
        const auto correct = QString::fromStdString(server->code("email_fixture@example.test"));
        QVERIFY(correct.size() == 6);
        dialog->grab().save(evidence + "/04-registration-email-sent.png");
        Theme::instance().setMode(Theme::Dark);
        dialog->grab().save(evidence + "/05-registration-email-dark.png");
        Theme::instance().setMode(Theme::Light);
        auto fill = [&](const QString& code) {
            dialog->findChild<QLineEdit*>("accountPassword")->setText("MessagerTest-2026!");
            dialog->findChild<QLineEdit*>("accountConfirmation")->setText("MessagerTest-2026!");
            dialog->findChild<QLineEdit*>("registrationEmailCode")->setText(code);
            QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"), Qt::LeftButton);
        };
        QSignalSpy errors(&service, &ChatService::operationError);
        fill(correct == "000000" ? "111111" : "000000");
        QVERIFY(waitUntil([&] { return !errors.isEmpty(); }));
        QVERIFY(errors.last().at(0).toString().contains("验证码"));
        QVERIFY(!service.online());
        QVERIFY(!server->storage.findAccount("email_fixture"));
        QVERIFY(!service.configuration().contains("emailCode"));
        fill(correct);
        QVERIFY(waitUntil([&] { return service.online(); }));
        QCOMPARE(QString::fromStdString(server->storage.registrationEmail("email_fixture")), QString("email_fixture@example.test"));
    }
    void forgotPasswordThroughRealWidgets() {
        CipherChatService service(config("reset_widget"));
        AppController controller(&service); MainWindow window(&controller);
        signUp(window); QVERIFY(waitUntil([&]{return service.online();}));
        const auto identity=service.ownIdentity();
        service.disconnectAccount(); QVERIFY(waitUntil([&]{return !service.online();}));
        window.showLogin(); auto* dialog=window.accountDialog(); QVERIFY(dialog);
        QTest::mouseClick(dialog->findChild<QPushButton*>("forgotPassword"),Qt::LeftButton);
        QCOMPARE(dialog->windowTitle(),QString("CipherChat · 重置密码"));
        auto* password=dialog->findChild<QLineEdit*>("accountPassword");
        auto* confirmation=dialog->findChild<QLineEdit*>("accountConfirmation");
        auto* code=dialog->findChild<QLineEdit*>("registrationEmailCode");
        dialog->findChild<QLineEdit*>("registrationEmail")->setText("reset_widget@example.test");
        QTest::mouseClick(dialog->findChild<QPushButton*>("sendRegistrationCode"),Qt::LeftButton);
        QVERIFY(waitUntil([&]{return dialog->findChild<QLabel*>("registrationCodeStatus")->isVisible();}));
        password->setText("NewWidgetPassword!");confirmation->setText("NewWidgetPassword!");code->setText("invalid");
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"),Qt::LeftButton);
        QVERIFY(dialog->findChild<QPushButton*>("accountSubmit")->isEnabled());
        password->setText("NewWidgetPassword!");confirmation->setText("NewWidgetPassword!");
        const auto actual=QString::fromStdString(server->code("reset:reset_widget@example.test"));
        code->setText(actual=="000000" ? "111111" : "000000");
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"),Qt::LeftButton);
        QVERIFY(waitUntil([&]{return dialog->findChild<QPushButton*>("accountSubmit")->isEnabled();}));
        QVERIFY(dialog->findChild<QLabel*>("accountError")->text().contains("验证码"));
        password->setText("NewWidgetPassword!");confirmation->setText("NewWidgetPassword!");code->clear();
        QDir().mkpath(evidence+"/password-reset");
        Theme::instance().setMode(Theme::Light);QTest::qWait(100);dialog->grab().save(evidence+"/password-reset/reset-light.png");
        Theme::instance().setMode(Theme::Dark);QTest::qWait(100);dialog->grab().save(evidence+"/password-reset/reset-dark.png");
        code->clear(); // Screenshots contain no live verification code.
        code->setText(actual);
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"),Qt::LeftButton);
        QVERIFY(waitUntil([&]{return dialog->windowTitle()=="CipherChat · 登录";}));
        QVERIFY(password->text().isEmpty());
        QVERIFY(dialog->findChild<QLabel*>("accountError")->text().contains("密码已重置"));
        password->setText("NewWidgetPassword!");
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"),Qt::LeftButton);
        QVERIFY(waitUntil([&]{return service.online();}));
        QCOMPARE(service.ownIdentity(),identity);
    }
    void rememberedLoginIsProtectedAndScoped() {
        const auto connection = config("preferences_fixture");
        const QString password = "Only-a-test-fixture-2026!";
        LoginPreferences::clear(connection);
        QVERIFY(LoginPreferences::load(connection).username.isEmpty());
        QVERIFY(LoginPreferences::save(connection, "preferences_fixture", password, true,
                                       LoginPreferences::supportsPassword()));
        auto saved = LoginPreferences::load(connection);
        QCOMPARE(saved.username, QString("preferences_fixture"));
        if (LoginPreferences::supportsPassword())
            QVERIFY(saved.password == password);
        QFile settings(QSettings().fileName());
        QVERIFY(settings.open(QIODevice::ReadOnly));
        QVERIFY(!settings.readAll().contains(password.toUtf8()));
        auto otherServer = connection;
        otherServer["host"] = "different-server.invalid";
        QVERIFY(LoginPreferences::load(otherServer).username.isEmpty());
        CipherChatService service(connection);
        AppController controller(&service);
        MainWindow window(&controller);
        window.showLogin();
        auto* dialog = window.accountDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QLineEdit*>("accountUsername")->text(), saved.username);
        if (LoginPreferences::supportsPassword()) {
            QVERIFY(dialog->findChild<QLineEdit*>("accountPassword")->text() == password);
            auto* remember = dialog->findChild<QCheckBox*>("rememberPassword");
            remember->setChecked(false);
            QVERIFY(LoginPreferences::load(connection).password.isEmpty());
        }
        dialog->findChild<QCheckBox*>("rememberUsername")->setChecked(false);
        QVERIFY(LoginPreferences::load(connection).username.isEmpty());
        dialog->reject();
    }
    void temporaryConversationAndFriendRequestLifecycle() {
        CipherChatService alice(config("temp_alice")), bob(config("temp_bob"));
        AppController ac(&alice), bc(&bob);
        MainWindow aw(&ac), bw(&bc);
        signUp(aw);
        signUp(bw);
        QVERIFY(waitUntil([&] { return alice.online() && bob.online(); }));
        QVERIFY(!alice.isFriend("temp_bob"));
        QVERIFY(!alice.canSendFiles("missing"));
        aw.showPage(1);
        aw.findChild<QLineEdit*>("serverUserSearch")->setText("temp_bob");
        QTest::mouseClick(aw.findChild<QPushButton*>("searchUsersButton"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return visibleButton(aw, "temporaryChat_temp_bob") != nullptr; }));
        QCOMPARE(visibleButton(aw, "temporaryChat_temp_bob")->text(), QString("临时会话"));
        QTest::mouseClick(visibleButton(aw, "temporaryChat_temp_bob"), Qt::LeftButton);
        QCOMPARE(ac.selectedId(), QString("peer:temp_bob"));
        QCOMPARE(aw.findChild<QStackedWidget*>("pages")->currentIndex(), 0);
        QVERIFY(alice.conversation(ac.selectedId()).subtitle.contains("临时会话"));
        QVERIFY(!aw.findChild<QPushButton*>("attachButton")->isEnabled());
        QVERIFY(aw.findChild<QPushButton*>("attachButton")->toolTip().contains("文字"));
        QVERIFY(!alice.canSendFiles(ac.selectedId()));

        const QString path = root + "/friend-only-file.txt";
        const QByteArray payload("File sent only after friendship is accepted.");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), payload.size());
        file.close();
        QSignalSpy errors(&alice, &ChatService::operationError);
        ac.attachFile(path);
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains("成为好友"));
        QTest::qWait(150);
        QVERIFY(alice.transfers().isEmpty());
        QVERIFY(bob.transfers().isEmpty());

        auto* editor = aw.findChild<MessageEditor*>("messageInput");
        editor->setPlainText("Text before becoming friends");
        QVERIFY(aw.findChild<QPushButton*>("sendButton")->isEnabled());
        QTest::keyClick(editor, Qt::Key_Return);
        int confirmations = 0;
        QVERIFY(waitUntil([&] {
            compareAndApprove(aw, "temp_bob", bob.ownIdentity(), confirmations);
            compareAndApprove(bw, "temp_alice", alice.ownIdentity(), confirmations);
            return textCount(bob, "peer:temp_alice", "Text before becoming friends") == 1 &&
                   alice.messages("peer:temp_bob").last().transportStatus == "对端已签收";
        }));
        Theme::instance().setMode(Theme::Dark);
        QTest::qWait(60);
        aw.grab().save(evidence + "/temporary-chat-text-only.png");

        aw.showPage(1);
        QVERIFY(visibleButton(aw, "addFriend_temp_bob"));
        QTest::mouseClick(visibleButton(aw, "addFriend_temp_bob"), Qt::LeftButton);
        QVERIFY(alice.friendRequestPending("temp_bob") || alice.outgoingRequests().contains("temp_bob"));
        QVERIFY(waitUntil([&] {
            auto* add = visibleButton(aw, "addFriend_temp_bob");
            return alice.outgoingRequests().contains("temp_bob") && add && add->text() == "已申请" &&
                   !add->isEnabled() && bob.incomingRequests().contains("temp_alice");
        }));
        QVERIFY(visibleButton(aw, "temporaryChat_temp_bob")->isEnabled());
        aw.grab().save(evidence + "/friend-request-applied.png");
        const auto activityCount = alice.activities().size();
        alice.addFriend("temp_bob");
        alice.addFriend("temp_bob");
        QTest::qWait(150);
        QCOMPARE(alice.activities().size(), activityCount);
        QTest::mouseClick(aw.findChild<QPushButton*>("searchUsersButton"), Qt::LeftButton);
        QTest::qWait(150);
        QCOMPARE(visibleButton(aw, "addFriend_temp_bob")->text(), QString("已申请"));

        bw.showPage(1);
        QVERIFY(waitUntil([&] { return visibleButton(bw, "rejectFriend_temp_alice") != nullptr; }));
        QTest::mouseClick(visibleButton(bw, "rejectFriend_temp_alice"), Qt::LeftButton);
        QVERIFY(waitUntil([&] {
            auto* add = visibleButton(aw, "addFriend_temp_bob");
            return !alice.outgoingRequests().contains("temp_bob") && add && add->text() == "添加好友" &&
                   add->isEnabled();
        }));
        QTest::mouseClick(visibleButton(aw, "addFriend_temp_bob"), Qt::LeftButton);
        QVERIFY(waitUntil([&] {
            return bob.incomingRequests().contains("temp_alice") &&
                   visibleButton(bw, "acceptFriend_temp_alice") != nullptr;
        }));
        QTest::mouseClick(visibleButton(bw, "acceptFriend_temp_alice"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return alice.isFriend("temp_bob") && bob.isFriend("temp_alice"); }));
        QVERIFY(waitUntil([&] {
            auto* add = visibleButton(aw, "addFriend_temp_bob");
            return add && add->text() == "已是好友" && !add->isEnabled();
        }));
        QCOMPARE(visibleButton(aw, "temporaryChat_temp_bob")->text(), QString("发消息"));
        aw.showPage(0);
        QVERIFY(aw.findChild<QPushButton*>("attachButton")->isEnabled());
        QVERIFY(alice.canSendFiles(ac.selectedId()));
        QVERIFY(!alice.conversation(ac.selectedId()).subtitle.contains("临时会话"));
        QCOMPARE(textCount(alice, ac.selectedId(), "Text before becoming friends"), 1);
        ac.attachFile(path);
        QString received;
        QVERIFY(waitUntil([&] {
            for (const auto& transfer : bob.transfers())
                if (transfer.name == "friend-only-file.txt" && transfer.state == TransferState::Completed) {
                    received = transfer.path;
                    return true;
                }
            return false;
        }));
        QFile incoming(received);
        QVERIFY(incoming.open(QIODevice::ReadOnly));
        QCOMPARE(incoming.readAll(), payload);

        errors.clear();
        alice.addFriend("no_such_friend_fixture");
        QVERIFY(alice.friendRequestPending("no_such_friend_fixture"));
        QVERIFY(waitUntil(
            [&] { return !alice.friendRequestPending("no_such_friend_fixture") && !errors.isEmpty(); }));
        QVERIFY(!alice.outgoingRequests().contains("no_such_friend_fixture"));
        alice.addFriend("no_such_friend_fixture");
        QVERIFY(alice.friendRequestPending("no_such_friend_fixture"));
        QVERIFY(waitUntil([&] { return !alice.friendRequestPending("no_such_friend_fixture"); }));
    }
    void realBackendThroughWidgets() {
        CipherChatService alice(config("msg_alice")), bob(config("msg_bob"));
        AppController ac(&alice), bc(&bob);
        MainWindow aw(&ac), bw(&bc);
        signUp(aw);
        QVERIFY(!aw.isVisible());
        signUp(bw);
        QVERIFY(!bw.isVisible());
        QVERIFY2(waitUntil([&] { return alice.online() && bob.online(); }),
                 "Real TLS registration/login failed");
        QVERIFY(aw.isVisible());
        QVERIFY(bw.isVisible());
        QVERIFY(alice.ownIdentity().isEmpty());
        const auto originalIdentity = alice.ownIdentity();
        aw.showPage(1);
        aw.findChild<QLineEdit*>("serverUserSearch")->setText("msg_bob");
        QTest::mouseClick(aw.findChild<QPushButton*>("searchUsersButton"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return !alice.searchResults().isEmpty(); }));
        QVERIFY(waitUntil([&] { return aw.findChild<QPushButton*>("addFriend_msg_bob") != nullptr; }));
        QTest::mouseClick(aw.findChild<QPushButton*>("addFriend_msg_bob"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return bob.incomingRequests().contains("msg_alice"); }));
        bw.showPage(1);
        QVERIFY(waitUntil([&] { return bw.findChild<QPushButton*>("acceptFriend_msg_alice") != nullptr; }));
        QTest::mouseClick(bw.findChild<QPushButton*>("acceptFriend_msg_alice"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return !alice.contacts().isEmpty() && !bob.contacts().isEmpty(); }));
        ac.selectConversation("peer:msg_bob");
        aw.showPage(0);
        auto* edit = aw.findChild<MessageEditor*>("messageInput");
        edit->setPlainText("Real CipherChat message from Messager");
        QTest::keyClick(edit, Qt::Key_Return);
        int confirmations = 0;
        QVERIFY(waitUntil([&] {
            compareAndApprove(aw, "msg_bob", bob.ownIdentity(), confirmations);
            compareAndApprove(bw, "msg_alice", alice.ownIdentity(), confirmations);
            return textCount(bob, "peer:msg_alice", "Real CipherChat message from Messager") == 1 &&
                   !alice.messages("peer:msg_bob").isEmpty() &&
                   alice.messages("peer:msg_bob").last().transportStatus == "对端已签收";
        }));
        QCOMPARE(confirmations, 0);
        QCOMPARE(textCount(alice, "peer:msg_bob", "Real CipherChat message from Messager"), 1);
        bc.selectConversation("peer:msg_alice");
        bw.showPage(0);
        auto* reply = bw.findChild<MessageEditor*>("messageInput");
        reply->setPlainText("Qt Widgets reply");
        reply->moveCursor(QTextCursor::End);
        QTest::keyClick(reply, Qt::Key_Return, Qt::ShiftModifier);
        reply->insertPlainText("second line");
        QTest::keyClick(reply, Qt::Key_Return);
        QVERIFY(waitUntil(
            [&] { return textCount(alice, "peer:msg_bob", "Qt Widgets reply\nsecond line") == 1; }));
        aw.showPage(1);
        QTest::mouseClick(aw.findChild<QPushButton*>("createGroupButton"), Qt::LeftButton);
        auto* group = aw.findChild<QDialog*>("createGroupDialog");
        QVERIFY(group);
        auto* createGroup = group->findChild<QPushButton*>("createGroupSubmit");
        auto* members = group->findChild<GroupMemberPicker*>("groupMembers");
        QVERIFY(createGroup && members);
        QVERIFY(!createGroup->isEnabled());
        group->findChild<QLineEdit*>("groupName")->setText("Messager integration group");
        QVERIFY(!createGroup->isEnabled());
        QTest::mouseClick(members, Qt::LeftButton);
        auto* memberPopup = members->findChild<QMenu*>("groupMembersPopup");
        QVERIFY(waitUntil([&] { return memberPopup && memberPopup->isVisible(); }));
        auto* bobMember = members->findChild<QCheckBox*>("groupMember_msg_bob");
        QVERIFY(bobMember);
        QVERIFY(!members->findChild<QCheckBox*>("groupMember_msg_alice"));
        QTest::mouseClick(bobMember, Qt::LeftButton, Qt::NoModifier, QPoint(10, bobMember->height() / 2));
        QVERIFY(memberPopup->isVisible());
        QCOMPARE(members->selectedMembers(), QStringList({"msg_bob"}));
        QVERIFY(createGroup->isEnabled());
        Theme::instance().setMode(Theme::Dark);
        QTest::qWait(40);
        group->grab().save(evidence + "/group-dialog-dark.png");
        memberPopup->grab().save(evidence + "/group-dropdown-dark.png");
        QTest::mouseClick(members->findChild<QPushButton*>("groupMemberDone"), Qt::LeftButton);
        QVERIFY(!memberPopup->isVisible());
        group->grab().save(evidence + "/group-selected-dark.png");
        Theme::instance().setMode(Theme::Light);
        group->grab().save(evidence + "/group-selected-light.png");
        QTest::mouseClick(createGroup, Qt::LeftButton);
        QString groupId;
        QVERIFY(waitUntil([&] {
            for (const auto& c : alice.conversations())
                if (c.group) {
                    groupId = c.id;
                    return true;
                }
            return false;
        }));
        QVERIFY(waitUntil([&] { return !bob.conversation(groupId).id.isEmpty(); }));
        ac.selectConversation(groupId);
        aw.showPage(0);
        edit->setPlainText("Real group message");
        QTest::keyClick(edit, Qt::Key_Return);
        QVERIFY(waitUntil([&] { return textCount(bob, groupId, "Real group message") == 1; }));
        QCOMPARE(textCount(alice, groupId, "Real group message"), 1);
        QCOMPARE(alice.conversation(groupId).members.size(), 2);
        ac.selectConversation("peer:msg_bob");
        aw.showPage(0);
        QByteArray payload(3 * 65536 + 137, '\0');
        for (qsizetype i = 0; i < payload.size(); ++i)
            payload[i] = char(i % 251);
        const QString payloadName = "集成传输, payload.bin";
        QString source = root + "/" + payloadName;
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), payload.size());
        file.close();
        ac.attachFile(source);
        QString fileId, received;
        QVERIFY(waitUntil(
            [&] {
                for (const auto& t : bob.transfers())
                    if (t.name == payloadName && t.state == TransferState::Completed) {
                        fileId = t.id;
                        received = t.path;
                        return true;
                    }
                return false;
            },
            45000));
        QFile incoming(received);
        QVERIFY(incoming.open(QIODevice::ReadOnly));
        auto receivedBytes = incoming.readAll();
        QCOMPARE(receivedBytes, payload);
        auto hash =
            QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        aw.showPage(2);
        QTest::qWait(120);
        aw.grab().save(evidence + "/live-files.png");
        auto* locateFile = aw.findChild<QPushButton*>("transferLocate_" + fileId);
        auto* revealFile = aw.findChild<QPushButton*>("transferReveal_" + fileId);
        QVERIFY(locateFile && locateFile->isEnabled());
        QVERIFY(revealFile && revealFile->isEnabled());
        if (qEnvironmentVariable("MESSAGER_VERIFY_EXPLORER") == "1") {
            QSignalSpy errors(&alice, &ChatService::operationError);
            QTest::mouseClick(revealFile, Qt::LeftButton);
            QVERIFY(errors.isEmpty());
            saveJson("explorer-selection-request.json", {{"directory", QFileInfo(source).absolutePath()},
                                                         {"fileName", payloadName},
                                                         {"file", source}});
        }
        aw.findChild<QLineEdit*>("messageSearch")->setText("nonmatching search filter");
        QTest::mouseClick(locateFile, Qt::LeftButton);
        QCOMPARE(ac.selectedId(), QString("peer:msg_bob"));
        QCOMPARE(aw.findChild<QStackedWidget*>("pages")->currentIndex(), 0);
        QVERIFY(aw.findChild<QLineEdit*>("messageSearch")->text().isEmpty());
        auto* messageScroll = aw.findChild<QScrollArea*>("messageScroll");
        bool located = false;
        for (auto* row :
             messageScroll->widget()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (row->property("messageId").toString() == "file:" + fileId) {
                auto* bubble = row->findChild<MessageBubble*>();
                const QRect bounds(row->mapTo(messageScroll->viewport(), QPoint()), row->size());
                located = bubble && bubble->property("locatedMessage").toBool() &&
                          messageScroll->viewport()->rect().contains(bounds);
            }
        QVERIFY(located);
        aw.grab().save(evidence + "/file-message-location.png");
        aw.showPage(2);
        // Interrupt a genuine unfinished checkpoint, then log in to the same account and resume it.
        QByteArray large(12 * 1024 * 1024, 'R');
        QString largePath = root + "/resume payload.bin";
        QFile largeFile(largePath);
        QVERIFY(largeFile.open(QIODevice::WriteOnly));
        QCOMPARE(largeFile.write(large), large.size());
        largeFile.close();
        ac.attachFile(largePath);
        QString resumeId;
        QVERIFY(waitUntil([&] {
            for (const auto& t : alice.transfers())
                if (t.name == "resume payload.bin" && t.state != TransferState::Completed) {
                    resumeId = t.id;
                    return true;
                }
            return false;
        }));
        alice.disconnectAccount();
        QTest::qWait(50);
        QVERIFY(alice.conversations().isEmpty());
        QCOMPARE(aw.findChild<ConversationModel*>()->rowCount(), 0);
        QVERIFY(aw.findChildren<TransferCard*>().isEmpty());
        auto login = config("msg_alice");
        login["password"] = "MessagerTest-2026!";
        login["registerNew"] = false;
        alice.authenticate(login);
        QVERIFY(waitUntil([&] { return alice.online(); }));
        QCOMPARE(alice.ownIdentity()["fingerprint"].toString(), originalIdentity["fingerprint"].toString());
        QVERIFY(textCount(alice, "peer:msg_bob", "Real CipherChat message from Messager") == 1);
        QVERIFY(textCount(alice, groupId, "Real group message") == 1);
        QVERIFY(waitUntil([&] {
            for (const auto& t : alice.transfers())
                if (t.id == resumeId)
                    return t.state == TransferState::Paused;
            return false;
        }));
        alice.transferAction(resumeId, "resume");
        QString resumedPath;
        QVERIFY(waitUntil(
            [&] {
                for (const auto& t : bob.transfers())
                    if (t.id == resumeId && t.state == TransferState::Completed) {
                        resumedPath = t.path;
                        return true;
                    }
                return false;
            },
            45000));
        QFile resumed(resumedPath);
        QVERIFY(resumed.open(QIODevice::ReadOnly));
        QCOMPARE(resumed.readAll(), large);
        ac.selectConversation("peer:msg_bob");
        QString cancelPath = root + "/cancel payload.bin";
        QVERIFY(QFile::copy(largePath, cancelPath));
        ac.attachFile(cancelPath);
        QString cancelId;
        QVERIFY(waitUntil([&] {
            for (const auto& t : alice.transfers())
                if (t.name == "cancel payload.bin" && t.state == TransferState::Running) {
                    cancelId = t.id;
                    return true;
                }
            return false;
        }));
        aw.showPage(2);
        auto* cancel = aw.findChild<QPushButton*>("transferCancel_" + cancelId);
        QVERIFY(cancel);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(waitUntil([&] {
            for (const auto& t : alice.transfers())
                if (t.id == cancelId)
                    return t.state == TransferState::Cancelled;
            return false;
        }));
        ac.selectConversation("peer:msg_bob");
        aw.showPage(0);
        QTest::qWait(150);
        aw.grab().save(evidence + "/live-chat.png");
        aw.setDetailsVisible(true);
        QTest::qWait(80);
        aw.grab().save(evidence + "/live-details.png");
        aw.setDetailsVisible(false);
        Theme::instance().setMode(Theme::Dark);
        QTest::qWait(80);
        aw.grab().save(evidence + "/live-chat-dark.png");
        Theme::instance().setMode(Theme::Light);
        aw.resize(960, 640);
        QTest::qWait(120);
        QCOMPARE(aw.size(), QSize(960, 640));
        aw.grab().save(evidence + "/live-small.png");
        aw.resize(1280, 800);
        aw.showPage(2);
        QTest::qWait(80);
        aw.grab().save(evidence + "/live-files-final.png");
        aw.showPage(1);
        QTest::qWait(100);
        aw.grab().save(evidence + "/live-contacts.png");
        ac.selectConversation(groupId);
        aw.showPage(0);
        QTest::qWait(80);
        aw.grab().save(evidence + "/live-group.png");
        saveJson("live-check.json",
                 {{"backend", "actual CipherChat TLS server and BusinessWorker"},
                  {"loopbackOnly", true},
                  {"aliceOnline", alice.online()},
                  {"bobOnline", bob.online()},
                  {"identityComparisons", confirmations},
                  {"privateMessageCount",
                   textCount(alice, "peer:msg_bob", "Real CipherChat message from Messager")},
                  {"groupMessageCount", textCount(alice, groupId, "Real group message")},
                  {"fileBytes", payload.size()},
                  {"sourceAndReceivedSha256", hash},
                  {"resumedFileBytes", large.size()},
                  {"cancelConfirmed", true},
                  {"historyRestored", true},
                  {"identityPreserved", true}});
        aw.showAccountDialog();
        auto* account = aw.findChild<QDialog*>("accountManagement");
        QVERIFY(account);
        QTest::mouseClick(account->findChild<QPushButton*>("logoutButton"), Qt::LeftButton);
        QVERIFY(!aw.isVisible());
        QVERIFY(aw.accountDialog()->isVisible());
        cancelConnectionSettings(aw.accountDialog());
        aw.accountDialog()->findChild<QLineEdit*>("accountPassword")->setText("MessagerTest-2026!");
        QTest::mouseClick(aw.accountDialog()->findChild<QPushButton*>("accountSubmit"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return alice.online(); }));
        QVERIFY(aw.isVisible());
    }
    void certificateNameMismatchIsRejected() {
        CipherChatService wrong(config("tls_rejected"));
        auto request = config("tls_rejected");
        request["serverName"] = "wrong.invalid";
        request["password"] = "MessagerTest-2026!";
        request["registerNew"] = true;
        QSignalSpy errors(&wrong, &ChatService::operationError);
        wrong.authenticate(request);
        QVERIFY(waitUntil([&] { return !errors.isEmpty(); }, 12000));
        QVERIFY(!wrong.online());
    }
    void wrongPasswordIsRejected() {
        auto request = config("msg_alice");
        request["password"] = "Incorrect-fixture-password!";
        request["registerNew"] = false;
        CipherChatService wrong(request);
        QSignalSpy errors(&wrong, &ChatService::operationError);
        AppController controller(&wrong);
        MainWindow window(&controller);
        window.showLogin();
        auto* dialog = window.accountDialog();
        QVERIFY(dialog);
        cancelConnectionSettings(dialog);
        dialog->findChild<QLineEdit*>("accountPassword")->setText(request["password"].toString());
        QTest::mouseClick(dialog->findChild<QPushButton*>("accountSubmit"), Qt::LeftButton);
        QVERIFY(waitUntil([&] { return !errors.isEmpty(); }, 12000));
        QVERIFY(!wrong.online());
        QVERIFY(!wrong.configuration().contains("password"));
        QVERIFY(errors.last().at(0).toString().contains("登录失败"));
        QVERIFY(!window.isVisible());
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->findChild<QPushButton*>("accountSubmit")->isEnabled());
    }
    void chatAvatarsRefreshWithoutOpeningProfile() {
        CipherChatService a(config("avatar_alice")),b(config("avatar_bob"));
        AppController ca(&a),cb(&b);MainWindow wa(&ca),wb(&cb);
        wa.show();wb.show();signUp(wa);QVERIFY(waitUntil([&]{return a.online();}));signUp(wb);QVERIFY(waitUntil([&]{return b.online();}));
        auto png=[](QColor color){QImage image(96,96,QImage::Format_RGB32);image.fill(color);QByteArray data;QBuffer out(&data);out.open(QIODevice::WriteOnly);image.save(&out,"PNG");return QString::fromLatin1(data.toBase64());};
        const auto green=png(QColor("#239b8d")),red=png(QColor("#d45667"));
        b.saveProfile("头像同步测试",green);QVERIFY(waitUntil([&]{return b.profile(b.username())["avatar"].toString()==green;}));
        a.addFriend(b.username());QVERIFY(waitUntil([&]{return b.incomingRequests().contains(a.username());}));b.respondFriend(a.username(),true);
        QVERIFY(waitUntil([&]{return a.isFriend(b.username());}));ca.openDirectConversation(b.username());
        auto* list=wa.findChild<QListView*>("conversationList");QVERIFY(list);
        auto index=[&]{for(int row=0;row<list->model()->rowCount();++row){auto i=list->model()->index(row,0);if(i.data(ConversationModel::Id).toString()=="peer:"+b.username())return i;}return QModelIndex{};};
        QVERIFY(waitUntil([&]{return !index().data(ConversationModel::AvatarImage).value<QPixmap>().isNull();},22000));
        QCOMPARE(index().data(ConversationModel::Name).toString(),QString("头像同步测试"));
        QVERIFY(a.profile(b.username()).isEmpty()); // No personal page or explicit profile request.
        auto imageColor=[&]{return index().data(ConversationModel::AvatarImage).value<QPixmap>().toImage().pixelColor(44,44);};
        QCOMPARE(imageColor(),QColor("#239b8d"));
        b.send("peer:"+a.username(),"avatar message");
        int confirmations=0;
        QVERIFY(waitUntil([&]{
            compareAndApprove(wa,b.username(),b.ownIdentity(),confirmations);
            compareAndApprove(wb,a.username(),a.ownIdentity(),confirmations);
            return textCount(a,"peer:"+b.username(),"avatar message")==1;
        }));
        const auto received=a.messages("peer:"+b.username());
        auto* messageAvatar=wa.findChild<QWidget*>("messageAvatar_"+received.last().id);QVERIFY(messageAvatar);
        QCOMPARE(messageAvatar->grab().toImage().pixelColor(15,15),QColor("#239b8d"));
        auto* edit=wa.findChild<MessageEditor*>("messageInput");edit->setPlainText("头像刷新时保留的草稿");
        auto cursor=edit->textCursor();cursor.setPosition(1);cursor.setPosition(5,QTextCursor::KeepAnchor);edit->setTextCursor(cursor);
        const auto selection=edit->textCursor().selectedText();
        auto* scroll=wa.findChild<QScrollArea*>("messageScroll");QPointer<QWidget> transcript=scroll->widget();
        const auto position=scroll->verticalScrollBar()->value();
        QSignalSpy resets(list->model(),&QAbstractItemModel::modelReset);
        b.saveProfile("头像已更新",red);QVERIFY(waitUntil([&]{return imageColor()==QColor("#d45667");},22000));
        QCOMPARE(index().data(ConversationModel::Name).toString(),QString("头像已更新"));
        QCOMPARE(resets.count(),0);QCOMPARE(scroll->widget(),transcript.data());QCOMPARE(scroll->verticalScrollBar()->value(),position);
        QCOMPARE(edit->toPlainText(),QString("头像刷新时保留的草稿"));QCOMPARE(edit->textCursor().selectedText(),selection);
        auto* header=wa.findChild<QWidget*>("chatProfileAvatar");QVERIFY(header);
        QCOMPARE(header->grab().toImage().pixelColor(header->width()/2,header->height()/2),QColor("#d45667"));
        QCOMPARE(messageAvatar->grab().toImage().pixelColor(15,15),QColor("#d45667"));
        for(auto skin:{Theme::DefaultSkin,Theme::PencilSkin})for(auto mode:{Theme::Light,Theme::Dark}) {
            Theme::instance().setSkin(skin);Theme::instance().setMode(mode);QTest::qWait(60);
            QVERIFY(wa.grab().save(evidence+QString("/avatar-sync-%1-%2.png").arg(skin==Theme::PencilSkin?"pencil":"default",mode==Theme::Dark?"dark":"light")));
        }
        b.saveProfile(b.username(),{});QVERIFY(waitUntil([&]{return a.profileSummary(b.username())["avatar"].toString().isEmpty();},22000));
        QVERIFY(index().data(ConversationModel::AvatarImage).value<QPixmap>().isNull());
        QCOMPARE(edit->toPlainText(),QString("头像刷新时保留的草稿"));
        a.disconnectAccount();QVERIFY(waitUntil([&]{return !a.online();}));QVERIFY(a.profileSummary(b.username()).isEmpty());
        Theme::instance().setSkin(qEnvironmentVariable("MESSAGER_TEST_SKIN")=="pencil"?Theme::PencilSkin:Theme::DefaultSkin);
        Theme::instance().setMode(Theme::Light);
    }
    void profileAndMomentsThroughRealWidgets() {
        const auto originalAccountScope="127.0.0.1:"+QString::number(server->tls.port())+"/localhost";
        CipherChatService a(config("profile_alice")),b(config("profile_bob")),stranger(config("profile_stranger"));
        AppController ca(&a),cb(&b),cs(&stranger);MainWindow wa(&ca),wb(&cb),ws(&cs);
        wa.show();wb.show();ws.show();signUp(wa);QVERIFY(waitUntil([&]{return a.online();}));signUp(wb);QVERIFY(waitUntil([&]{return b.online();}));signUp(ws);QVERIFY(waitUntil([&]{return stranger.online();}));
        QTest::mouseClick(wa.findChild<QPushButton*>("profileButton"),Qt::LeftButton);
        auto* page=wa.findChild<ProfilePage*>();QVERIFY(page&&page->isVisible());QVERIFY(waitUntil([&]{return !a.profile(a.username()).isEmpty();}));
        const auto avatarPath=root+"/profile-avatar.png";QImage image(360,280,QImage::Format_ARGB32);image.fill(QColor("#2d9b91"));{QPainter p(&image);p.setBrush(QColor("#f0dfbc"));p.setPen(Qt::NoPen);p.drawEllipse(130,50,100,100);p.drawRoundedRect(85,160,190,150,70,70);}QVERIFY(image.save(avatarPath));
        QVERIFY(page->chooseAvatarFile(avatarPath));auto* nickname=page->findChild<QLineEdit*>("profileNicknameEdit");nickname->selectAll();QInputMethodEvent input;input.setCommitString("小林");QApplication::sendEvent(nickname,&input);
        QTest::mouseClick(page->findChild<QPushButton*>("saveProfileButton"),Qt::LeftButton);QVERIFY(waitUntil([&]{return a.profile(a.username())["nickname"].toString()=="小林";}));QVERIFY(!a.profile(a.username())["avatar"].toString().isEmpty());
        auto* editor=page->findChild<QPlainTextEdit*>("momentText");auto* publish=page->findChild<QPushButton*>("publishMomentButton");
        editor->setPlainText("<b>第一条朋友圈</b>\n今天开始记录生活。");QTest::mouseClick(publish,Qt::LeftButton);QVERIFY(waitUntil([&]{return a.profile(a.username())["posts"].toArray().size()==1&&editor->toPlainText().isEmpty();}));
        editor->setPlainText("今天的风很舒服。\n把喜欢的小事记下来，也分享给朋友。");QTest::mouseClick(publish,Qt::LeftButton);QVERIFY(waitUntil([&]{return a.profile(a.username())["posts"].toArray().size()==2&&editor->toPlainText().isEmpty();}));
        auto posts=a.profile(a.username())["posts"].toArray();QVERIFY(posts[0].toObject()["created_ms"].toDouble()>posts[1].toObject()["created_ms"].toDouble());QVERIFY(posts[0].toObject()["text"].toString().startsWith("今天的风"));
        wb.showProfile(a.username());QVERIFY(waitUntil([&]{return !b.profile(a.username()).isEmpty();}));QVERIFY(!b.profile(a.username())["can_view"].toBool());QVERIFY(b.profile(a.username())["posts"].toArray().isEmpty());QCOMPARE(b.profile(a.username())["avatar"],a.profile(a.username())["avatar"]);
        b.addFriend(a.username());QVERIFY(waitUntil([&]{return a.incomingRequests().contains(b.username());}));a.respondFriend(b.username(),true);QVERIFY(waitUntil([&]{return b.isFriend(a.username());}));
        b.requestProfile(a.username());QVERIFY(waitUntil([&]{return b.profile(a.username())["posts"].toArray().size()==2;}));
        auto* friendPage=wb.findChild<ProfilePage*>();QVERIFY(friendPage);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(50);
        QVERIFY(!page->findChild<QPushButton*>("profileSendMessage")->isVisible());QVERIFY(friendPage->findChild<QPushButton*>("profileSendMessage")->isVisible());QTest::mouseClick(friendPage->findChild<QPushButton*>("profileSendMessage"),Qt::LeftButton);QCOMPARE(cb.selectedId(),"peer:"+a.username());QVERIFY(!friendPage->isVisible());wb.showProfile(a.username());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(!friendPage->findChild<QPushButton*>("saveProfileButton")->isVisible());auto bodies=friendPage->findChildren<QLabel*>("momentBody");QCOMPARE(bodies.size(),2);QVERIFY(bodies[0]->text().startsWith("今天的风"));QCOMPARE(bodies[1]->textFormat(),Qt::PlainText);QVERIFY(bodies[1]->text().contains("<b>"));
        Theme::instance().setMode(Theme::Light);QTest::qWait(100);wb.grab().save(evidence+"/profile-friend-light.png");wa.grab().save(evidence+"/profile-own-light.png");
        Theme::instance().setMode(Theme::Dark);QTest::qWait(100);wb.grab().save(evidence+"/profile-friend-dark.png");wa.grab().save(evidence+"/profile-own-dark.png");
        ws.showProfile(a.username());QVERIFY(waitUntil([&]{return !stranger.profile(a.username()).isEmpty();}));QVERIFY(stranger.profile(a.username())["posts"].toArray().isEmpty());ws.grab().save(evidence+"/profile-stranger-dark.png");
        b.publishMoment("朋友的最新近况。","bob-feed-post");QVERIFY(waitUntil([&]{return b.profile(b.username())["posts"].toArray().size()==1;}));
        stranger.publishMoment("仅我可见的陌生人动态。","stranger-feed-post");QVERIFY(waitUntil([&]{return stranger.profile(stranger.username())["posts"].toArray().size()==1;}));
        auto* nav=wb.findChild<QPushButton*>("momentsNav");QVERIFY(nav);QCOMPARE(nav->toolTip(),QString("朋友圈"));QTest::mouseClick(nav,Qt::LeftButton);
        QVERIFY(waitUntil([&]{return b.momentsFeed()["posts"].toArray().size()==3;}));QVERIFY(nav->isChecked());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto* feedPage=wb.findChild<QWidget*>("momentsFeedPage");QVERIFY(feedPage&&feedPage->isVisible());auto feedBodies=feedPage->findChildren<QLabel*>("feedMomentBody");QCOMPARE(feedBodies.size(),3);QCOMPARE(feedBodies[0]->text(),QString("朋友的最新近况。"));QVERIFY(feedBodies[2]->text().contains("<b>"));QCOMPARE(feedBodies[2]->textFormat(),Qt::PlainText);
        Theme::instance().setMode(Theme::Light);QTest::qWait(100);QVERIFY(wb.grab().save(evidence+"/moments-feed-light.png"));
        Theme::instance().setMode(Theme::Dark);QTest::qWait(100);QVERIFY(wb.grab().save(evidence+"/moments-feed-dark.png"));
        auto* author=feedPage->findChild<QPushButton*>("feedAuthor_"+a.username());QVERIFY(author);QTest::mouseClick(author,Qt::LeftButton);QVERIFY(friendPage->isVisible());QVERIFY(!nav->isChecked());QTest::mouseClick(friendPage->findChild<QPushButton*>("profileBack"),Qt::LeftButton);QVERIFY(feedPage->isVisible());QVERIFY(nav->isChecked());
        QTest::mouseClick(ws.findChild<QPushButton*>("momentsNav"),Qt::LeftButton);QVERIFY(waitUntil([&]{return stranger.momentsFeed()["posts"].toArray().size()==1;}));QCOMPARE(stranger.momentsFeed()["posts"].toArray()[0].toObject()["username"].toString(),stranger.username());
        QTest::mouseClick(wa.findChild<QPushButton*>("momentsNav"),Qt::LeftButton);QVERIFY(waitUntil([&]{return a.momentsFeed()["posts"].toArray().size()==3;}));
        QTest::mouseClick(wa.findChild<QPushButton*>("momentsFeedPublish"),Qt::LeftButton);QVERIFY(page->isVisible());QVERIFY(editor->isVisible());
        const auto identity=a.ownIdentity();a.disconnectAccount();b.disconnectAccount();stranger.disconnectAccount();QVERIFY(waitUntil([&]{return !a.online();}));QVERIFY(b.momentsFeed().isEmpty());QVERIFY(stranger.momentsFeed().isEmpty());server.reset();server=std::make_unique<LocalServer>(root+"/server");
        auto login=config("profile_alice");login["password"]="MessagerTest-2026!";login["accountScope"]=originalAccountScope;a.authenticate(login);QVERIFY(waitUntil([&]{return a.online()&&a.profile(a.username())["posts"].toArray().size()==2;}));QCOMPARE(a.profile(a.username())["nickname"].toString(),QString("小林"));QCOMPARE(a.ownIdentity(),identity);Theme::instance().setMode(Theme::Light);
    }
    void gomokuRoomsInviteAndPlay() {
        CipherChatService alice(config("gomoku_alice")),bob(config("gomoku_bob"));
        AppController ac(&alice),bc(&bob);MainWindow aw(&ac),bw(&bc);
        signUp(aw);QVERIFY(waitUntil([&]{return alice.online();}));
        signUp(bw);QVERIFY(waitUntil([&]{return bob.online();}));
        alice.addFriend("gomoku_bob");QVERIFY(waitUntil([&]{return bob.incomingRequests().contains("gomoku_alice");}));
        bob.respondFriend("gomoku_alice",true);QVERIFY(waitUntil([&]{return alice.isFriend("gomoku_bob")&&bob.isFriend("gomoku_alice");}));
        aw.show();bw.show();aw.showPage(7);auto* mini=aw.findChild<MiniAppsPage*>();mini->openApp("gomoku");
        auto* ap=aw.findChild<GomokuPage*>();auto* bp=bw.findChild<GomokuPage*>();QVERIFY(ap&&bp);
        auto* friendPicker=ap->findChild<QComboBox*>("gomokuFriends");QVERIFY(friendPicker);friendPicker->setCurrentIndex(friendPicker->findData("gomoku_bob"));
        QTest::mouseClick(ap->findChild<QPushButton*>("gomokuCreate"),Qt::LeftButton);
        QVERIFY(!ap->controller()->rooms().isEmpty());const auto id=ap->controller()->rooms().first().id;
        int confirmations=0;
        QVERIFY(waitUntil([&]{compareAndApprove(aw,"gomoku_bob",bob.ownIdentity(),confirmations);compareAndApprove(bw,"gomoku_alice",alice.ownIdentity(),confirmations);return !bp->controller()->room(id).id.isEmpty();}));
        bc.selectConversation("peer:gomoku_alice");bw.showPage(0);QTest::qWait(100);
        auto* invitation=bw.findChild<QPushButton*>("joinGomoku_"+id);QVERIFY(invitation&&invitation->isVisible());
        QVERIFY(bw.grab().save(evidence+"/gomoku-invitation.png"));
        QTest::mouseClick(invitation,Qt::LeftButton);
        QVERIFY(waitUntil([&]{return ap->controller()->room(id).started&&bp->controller()->room(id).started;}));
        QCOMPARE(bw.findChild<QStackedWidget*>("pages")->currentIndex(),7);
        QVERIFY(ap->controller()->canMove(id));QVERIFY(!bp->controller()->canMove(id));
        auto play=[&](GomokuPage* page,int x,int y,int count){
            auto* board=page->findChild<GomokuBoard*>();QVERIFY(board);QTest::mouseClick(board,Qt::LeftButton,{},board->cellCenter(x,y));
            QVERIFY(waitUntil([&]{return ap->controller()->room(id).moves.size()==count&&bp->controller()->room(id).moves.size()==count;}));
            QCOMPARE(ap->controller()->room(id).board,bp->controller()->room(id).board);
        };
        play(ap,0,0,1);play(bp,10,10,2);
        bob.disconnectAccount();QVERIFY(waitUntil([&]{return !bob.online();}));QVERIFY(!bp->controller()->canMove(id));
        auto login=config("gomoku_bob");login["password"]="MessagerTest-2026!";bob.authenticate(login);
        QVERIFY(waitUntil([&]{return bob.online()&&bp->controller()->room(id).moves.size()==2;}));
        bw.show();bw.showPage(7);bw.findChild<MiniAppsPage*>()->openApp("gomoku");
        // A white client cannot masquerade as the authoritative black host.
        auto forged=QJsonObject{{"type","state"},{"room",id},{"black","gomoku_alice"},{"white","gomoku_bob"},{"seq",9999},{"moves",QJsonArray{}},{"started",true},{"closed",false}};
        QVERIFY(!bob.send("peer:gomoku_alice",GomokuWire::encode(forged)).isEmpty());QTest::qWait(300);QCOMPARE(ap->controller()->room(id).moves.size(),2);
        play(ap,1,0,3);play(bp,10,11,4);play(ap,2,0,5);play(bp,10,12,6);play(ap,3,0,7);play(bp,11,10,8);play(ap,4,0,9);
        QCOMPARE(ap->controller()->room(id).winner,1);QCOMPARE(bp->controller()->room(id).winner,1);QVERIFY(!ap->controller()->canMove(id));QVERIFY(!bp->controller()->canMove(id));
        Theme::instance().setMode(Theme::Light);QTest::qWait(80);QVERIFY(aw.grab().save(evidence+"/gomoku-black-win.png"));QVERIFY(bw.grab().save(evidence+"/gomoku-white-view.png"));
        Theme::instance().setMode(Theme::Dark);QTest::qWait(80);QVERIFY(aw.grab().save(evidence+"/gomoku-dark.png"));
        Theme::instance().setSkin(Theme::PencilSkin);QTest::qWait(80);QVERIFY(aw.grab().save(evidence+"/gomoku-pencil.png"));Theme::instance().setSkin(Theme::DefaultSkin);Theme::instance().setMode(Theme::Light);
        QVERIFY(!ap->controller()->move(id,5,0));
        {GomokuController restored(&alice);QVERIFY(waitUntil([&]{return restored.room(id).winner==1;}));QCOMPARE(restored.room(id).moves.size(),9);}
        // Game control frames must not leak into the chat preview or unread counter.
        QVERIFY(!alice.conversation("peer:gomoku_bob").preview.contains(GomokuWire::marker()));
        const auto next=ap->controller()->create("gomoku_bob");QVERIFY(!next.isEmpty());
        QVERIFY(waitUntil([&]{return !bp->controller()->room(next).id.isEmpty();}));QVERIFY(bp->controller()->join(next));
        QVERIFY(waitUntil([&]{return ap->controller()->room(next).started&&bp->controller()->room(next).started;}));
        bp->controller()->leave(next);QVERIFY(waitUntil([&]{return ap->controller()->room(next).closed&&bp->controller()->room(next).closed;}));
        QCOMPARE(ap->controller()->room(next).resigned,QString("gomoku_bob"));QVERIFY(!bp->controller()->join(next));
    }
    void cleanupTestCase() {
        server.reset();
    }
};
QTEST_MAIN(LiveTest)
#include "tst_live.moc"
