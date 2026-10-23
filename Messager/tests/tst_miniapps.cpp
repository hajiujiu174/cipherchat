#include "services/mockservice.h"
#include "services/gomokucontroller.h"
#include "services/gomokuwire.h"
#include "../third_party/connectfour/rules.h"
#include "../third_party/reversi/rules.h"
#include "ui/gomokupage.h"
#include "ui/mainwindow.h"
#include "ui/miniappspage.h"
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
using namespace Messager;
class AccountService final : public ChatService {
  public:
    QString user = "alice";
    MockService mock;
    const QVector<Conversation>& conversations() const override {
        return mock.conversations();
    }
    QVector<Message> messages(const QString& id) const override {
        return mock.messages(id);
    }
    const QVector<Transfer>& transfers() const override {
        return mock.transfers();
    }
    Conversation conversation(const QString& id) const override {
        return mock.conversation(id);
    }
    QString send(const QString&, const QString&, bool) override {
        return {};
    }
    void retryMessage(const QString&) override {}
    void markViewed(const QString&) override {}
    void simulateIncoming(const QString&) override {}
    void addTransfer(const QString&, qint64, const QString&) override {}
    void sendFile(const QString&, const QString&) override {}
    void transferAction(const QString&, const QString&) override {}
    bool isDemo() const override {
        return false;
    }
    QString username() const override {
        return user;
    }
    QJsonObject configuration() const override {
        return {{"host", "test.invalid"}, {"port", 55220}};
    }
    void switchUser(const QString& name) {
        user = name;
        emit sessionChanged();
    }
};
class MiniAppsTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    QString evidence;
    void capture(QWidget& widget, const QString& name) {
        QTest::qWait(80);
        QVERIFY(widget.grab().save(evidence + "/" + name + ".png"));
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("MessagerMiniAppsTests");
        QCoreApplication::setApplicationName("MiniApps");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        evidence =
            qEnvironmentVariable("MINIAPPS_EVIDENCE", QDir::tempPath() + "/messager-miniapps-evidence");
        QVERIFY(QDir().mkpath(evidence));
        Theme::instance().setMode(Theme::Light);
    }
    void persistenceAndAccountIsolation() {
        AccountService service;
        {
            MiniAppsPage page(&service);
            page.resize(1100, 760);
            page.show();
            page.openApp("notes");
            page.findChild<QPlainTextEdit*>("miniAppNote")->setPlainText("Alice 的独立便签");
            // Switch before the autosave timer fires: it must flush to Alice, never Bob.
            service.switchUser("bob");
            QVERIFY(page.findChild<QPlainTextEdit*>("miniAppNote")->toPlainText().isEmpty());
            page.openApp("notes");
            page.findChild<QPlainTextEdit*>("miniAppNote")->setPlainText("Bob 的便签");
            service.switchUser("alice");
            QCOMPARE(page.findChild<QPlainTextEdit*>("miniAppNote")->toPlainText(),
                     QString("Alice 的独立便签"));
            page.openApp("todos");
            auto* input = page.findChild<QLineEdit*>("miniAppTodoInput");
            input->setText("检查小程序");
            QTest::keyClick(input, Qt::Key_Return);
            auto* list = page.findChild<QListWidget*>("miniAppTodos");
            QCOMPARE(list->count(), 1);
            list->item(0)->setCheckState(Qt::Checked);
            capture(page, "todos");
            service.switchUser("bob");
            QCOMPARE(list->count(), 0);
            service.switchUser("alice");
            QCOMPARE(list->count(), 1);
            QCOMPARE(list->item(0)->checkState(), Qt::Checked);
            page.openApp("notes");
            capture(page, "notes");
        }
        MiniAppsPage restored(&service);
        QCOMPARE(restored.findChild<QPlainTextEdit*>("miniAppNote")->toPlainText(),
                 QString("Alice 的独立便签"));
        QCOMPARE(restored.findChild<QListWidget*>("miniAppTodos")->count(), 1);
        restored.findChild<QPushButton*>("miniAppTodoClear")->click();
        QCOMPARE(restored.findChild<QListWidget*>("miniAppTodos")->count(), 0);
    }
    void calculator_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("expected");
        QTest::newRow("precedence") << "2+3*4" << "14";
        QTest::newRow("parentheses") << "(12 + 8) / 4" << "5";
        QTest::newRow("unary") << "-2 * (-3 + .5)" << "5";
        QTest::newRow("zero") << "0/2" << "0";
        for (const auto& invalid :
             QStringList{"1/0", "(1+2", "1..2", "", "2+", "2a", QString(80, '(') + "1" + QString(80, ')'),
                         QString(200, '9') + "*" + QString(200, '9')})
            QTest::newRow(qPrintable(invalid)) << invalid << "表达式无效，请检查括号、数字或除数。";
    }
    void calculator() {
        QFETCH(QString, expression);
        QFETCH(QString, expected);
        MockService service;
        MiniAppsPage page(&service);
        page.openApp("calculator");
        auto* input = page.findChild<QLineEdit*>("miniAppExpression");
        input->setText(expression);
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(page.findChild<QLabel*>("miniAppResult")->text(), expected);
    }
    void gomokuRulesAndWire() {
        for (auto direction : QVector<QPoint>{{1,0},{0,1},{1,1},{-1,1}}) {
            QJsonArray moves;
            for (int i=0;i<5;++i) {
                moves.append(QJsonArray{direction.x()<0?14-i:i*direction.x(),i*direction.y()});
                if(i<4)moves.append(QJsonArray{i,14});
            }
            GomokuRoom room;QVERIFY(GomokuController::replay(room,moves));QCOMPARE(room.winner,1);
            moves.append(QJsonArray{12,12});QVERIFY(!GomokuController::replay(room,moves));
        }
        for (const auto& moves : QList<QJsonArray>{QJsonArray{QJsonArray{-1,0}},QJsonArray{QJsonArray{15,0}},QJsonArray{QJsonArray{0.5,0}},QJsonArray{QJsonArray{true,0}},QJsonArray{QJsonArray{0,0},QJsonArray{0,0}}}) {
            GomokuRoom room;QVERIFY(!GomokuController::replay(room,moves));
        }
        QVERIFY(!GomokuWire::isControl("普通聊天消息"));QVERIFY(GomokuWire::decode("[[CipherChat:Gomoku:1]]{}").isEmpty());
        const QJsonObject object{{"type","invite"},{"room",QString(32,'a')},{"black","alice"},{"white","bob"}};
        QCOMPARE(GomokuWire::decode(GomokuWire::encode(object))["black"].toString(),QString("alice"));
        QVERIFY(!GomokuWire::isControl(GomokuWire::encode(object)));
        auto control=object;control["type"]="move";QVERIFY(GomokuWire::isControl(GomokuWire::encode(control)));
    }
    void connectFourRulesAndGravity() {
        for(auto direction:QVector<QPoint>{{1,0},{0,1},{1,1},{1,-1}}) {
            ConnectFourRules::Board board{};
            for(int i=0;i<4;++i)board[(direction.y()<0?5-i:i*direction.y())*7+i*direction.x()]=2;
            QVERIFY(ConnectFourRules::won(board,0,direction.y()<0?5:0));
        }
        GomokuRoom r;r.game="connectfour";QJsonArray moves;
        const QVector<QPoint> sequence{{0,5},{1,5},{0,4},{1,4},{0,3},{1,3},{0,2}};
        for(const auto& p:sequence)moves.append(QJsonArray{p.x(),p.y()});
        QVERIFY(GomokuController::replay(r,moves));QCOMPARE(r.winner,1);
        moves.append(QJsonArray{2,5});QVERIFY(!GomokuController::replay(r,moves));
        for(const auto& invalid:QList<QJsonArray>{QJsonArray{QJsonArray{0,0}},QJsonArray{QJsonArray{7,5}},QJsonArray{QJsonArray{1.5,5}}}) {
            GomokuRoom bad;bad.game="connectfour";QVERIFY(!GomokuController::replay(bad,invalid));
        }
        GomokuRoom full;full.game="connectfour";QJsonArray column;
        for(int y=5;y>=0;--y)column.append(QJsonArray{0,y});
        QVERIFY(GomokuController::replay(full,column));QCOMPARE(ConnectFourRules::dropRow(full.board,0),-1);
        QVERIFY(!GomokuController::legalMoves(full).contains(QPoint(0,0)));
    }
    void reversiFlipsPassAndFinish() {
        GomokuRoom initial;initial.game="reversi";QVERIFY(GomokuController::replay(initial,{}));
        QCOMPARE(GomokuController::legalMoves(initial).size(),4);
        QVERIFY(GomokuController::legalMoves(initial).contains(QPoint(3,2)));
        QVERIFY(!GomokuController::replay(initial,QJsonArray{QJsonArray{0,0}}));
        GomokuRoom one;one.game="reversi";
        QVERIFY(GomokuController::replay(one,QJsonArray{QJsonArray{3,2}}));
        QCOMPARE(std::count(one.board.begin(),one.board.end(),1),4);QCOMPARE(one.turn,2);
        ReversiRules::Board multi{};multi[3*8+1]=multi[1*8+3]=1;multi[3*8+2]=multi[2*8+3]=2;
        QVERIFY(ReversiRules::play(multi,3,3,1));QCOMPARE(multi[3*8+2],1);QCOMPARE(multi[2*8+3],1);
        bool sawPass=false;
        for(int seed=0;seed<4;++seed) {
            GomokuRoom r;r.game="reversi";QVERIFY(GomokuController::replay(r,{}));QJsonArray moves;
            while(!r.winner) {
                const auto legal=GomokuController::legalMoves(r);QVERIFY(!legal.isEmpty());
                const auto p=legal[(seed*7)%legal.size()];const int previous=r.turn;
                moves.append(QJsonArray{p.x(),p.y()});QVERIFY(GomokuController::replay(r,moves));
                if(!r.winner && r.turn==previous)sawPass=true;
                QVERIFY(moves.size()<=60);
            }
            QVERIFY(!ReversiRules::hasMove(r.board,1));QVERIFY(!ReversiRules::hasMove(r.board,2));
            const auto black=std::count(r.board.begin(),r.board.begin()+64,1),white=std::count(r.board.begin(),r.board.begin()+64,2);
            QCOMPARE(r.winner,black==white?3:black>white?1:2);
            moves.append(QJsonArray{0,0});QVERIFY(!GomokuController::replay(r,moves));
        }
        QVERIFY(sawPass);
    }
    void gameWireAndCatalogIsolation() {
        AccountService service;MiniAppsPage page(&service);page.resize(980,800);page.show();
        for(const auto& id:QStringList{"connectfour","reversi"}) {
            QJsonObject object{{"type","invite"},{"game",id},{"room",QString(32,'b')},{"black","alice"},{"white","bob"}};
            const auto encoded=GomokuWire::encode(object);
            QVERIFY(encoded.contains(GomokuWire::gameMarker()));QVERIFY(!encoded.contains(GomokuWire::marker()));
            QCOMPARE(GomokuWire::gameId(GomokuWire::decode(encoded)),id);
            object["type"]="move";QVERIFY(GomokuWire::isControl(GomokuWire::encode(object)));
            QVERIFY(GomokuWire::decode(GomokuWire::marker()+QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))).isEmpty());
            page.openApp(id);auto* game=page.findChild<GomokuPage*>(id+"Page");QVERIFY(game);QVERIFY(game->isVisible());
            QCOMPARE(game->controller()->gameId(),id);
            capture(page,id+"-light");Theme::instance().setMode(Theme::Dark);capture(page,id+"-dark");Theme::instance().setMode(Theme::Light);
        }
    }
    void scientificFunctions_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<bool>("radians");
        QTest::addColumn<double>("expected");
        QTest::newRow("sin-deg") << "sin(30)" << false << .5;
        QTest::newRow("cos-deg") << "cos(90)" << false << 0.0;
        QTest::newRow("tan-deg") << "tan(45)" << false << 1.0;
        QTest::newRow("sin-rad") << "sin(pi/2)" << true << 1.0;
        QTest::newRow("inverse-deg") << "asin(.5)" << false << 30.0;
        QTest::newRow("inverse-rad") << "acos(0)" << true << 1.5707963267948966;
        QTest::newRow("atan") << "atan(1)" << false << 45.0;
        QTest::newRow("logs") << "log(1000)+ln(e)" << false << 4.0;
        QTest::newRow("sqrt") << "sqrt(81)" << false << 9.0;
        QTest::newRow("exp") << "exp(0)+abs(-2)" << false << 3.0;
        QTest::newRow("right-power") << "2^3^2" << false << 512.0;
        QTest::newRow("unary-power") << "-2^2" << false << -4.0;
        QTest::newRow("negative-power") << "2^-2" << false << .25;
        QTest::newRow("factorial") << "5!+0!" << false << 121.0;
        QTest::newRow("percent") << "200*10%" << false << 20.0;
        QTest::newRow("scientific-notation") << "1e-3+2E+2" << false << 200.001;
        QTest::newRow("unicode-operators") << "6×3÷2−1" << false << 8.0;
    }
    void scientificFunctions() {
        QFETCH(QString, expression);
        QFETCH(bool, radians);
        QFETCH(double, expected);
        MockService service;
        MiniAppsPage page(&service);
        page.openApp("calculator");
        page.findChild<QComboBox*>("calculatorAngleMode")->setCurrentIndex(radians ? 1 : 0);
        auto* input = page.findChild<QLineEdit*>("miniAppExpression");
        input->setText(expression);
        QTest::keyClick(input, Qt::Key_Return);
        bool ok = false;
        double actual = page.findChild<QLabel*>("miniAppResult")->text().toDouble(&ok);
        QVERIFY(ok);
        QVERIFY2(qAbs(actual - expected) <= 1e-12 * qMax(1.0, qAbs(expected)), qPrintable(expression));
    }
    void scientificErrors() {
        MockService service;
        MiniAppsPage page(&service);
        page.openApp("calculator");
        auto* input = page.findChild<QLineEdit*>("miniAppExpression");
        for (const auto& text :
             QStringList{"sqrt(-1)", "ln(0)", "log(-2)", "asin(2)", "tan(90)", "171!", "2.5!", "(-1)!",
                         "exp(1000)", "sqrt(-1)^0", "ln(0)^0", "unknown(1)"}) {
            input->setText(text);
            QTest::keyClick(input, Qt::Key_Return);
            QVERIFY2(page.findChild<QLabel*>("miniAppResult")->text().startsWith("表达式无效"),
                     qPrintable(text));
        }
    }
    void scientificKeysAndAppearance() {
        MockService service;
        AppController controller(&service);
        MainWindow window(&controller);
        window.show();
        window.showPage(7);
        auto* page = window.findChild<MiniAppsPage*>();
        page->openApp("calculator");
        auto key = [page](const QString& token) {
            auto* b = page->findChild<QPushButton*>("calculatorKey_" + token);
            QVERIFY(b);
            QTest::mouseClick(b, Qt::LeftButton);
        };
        auto* input = page->findChild<QLineEdit*>("miniAppExpression");
        auto* result = page->findChild<QLabel*>("miniAppResult");
        key("sin(");
        key("3");
        key("0");
        key(")");
        page->findChild<QPushButton*>("miniAppCalculate")->click();
        QCOMPARE(result->text(), QString("0.5"));
        key("clear");
        key("Ans");
        key("*");
        key("2");
        page->findChild<QPushButton*>("miniAppCalculate")->click();
        QCOMPARE(result->text(), QString("1"));
        input->setText("4");
        key("reciprocal");
        page->findChild<QPushButton*>("miniAppCalculate")->click();
        QCOMPARE(result->text(), QString("0.25"));
        key("negate");
        page->findChild<QPushButton*>("miniAppCalculate")->click();
        QCOMPARE(result->text(), QString("-0.25"));
        input->setText("12");
        key("backspace");
        QCOMPARE(input->text(), QString("1"));
        input->setText("sin(30) + log(100) + sqrt(81)");
        page->findChild<QPushButton*>("miniAppCalculate")->click();
        Theme::instance().setSkin(Theme::DefaultSkin);
        Theme::instance().setMode(Theme::Light);
        capture(window, "scientific-light");
        window.resize(960, 640);
        QTest::qWait(80);
        auto* equals = page->findChild<QPushButton*>("miniAppCalculate");
        QCOMPARE(equals->visibleRegion().boundingRect(), equals->rect());
        capture(window, "scientific-minimum");
        window.resize(1280, 800);
        Theme::instance().setMode(Theme::Dark);
        capture(window, "scientific-dark");
        Theme::instance().setSkin(Theme::PencilSkin);
        capture(window, "scientific-pencil-dark");
        Theme::instance().setMode(Theme::Light);
        capture(window, "scientific-pencil");
        Theme::instance().setSkin(Theme::DefaultSkin);
    }
    void gamesEntryAndBackNavigation() {
        AccountService service;
        {
            MiniAppsPage page(&service);page.resize(1100,800);page.show();
            QVERIFY(!page.findChild<QPushButton*>("miniAppOpen_web"));
            QVERIFY(!page.findChild<QLineEdit*>("miniAppWebAddress"));
            auto* entry=page.findChild<QPushButton*>("miniAppOpen_games");QVERIFY(entry&&entry->isVisible());
            for(const auto& id:QStringList{"gomoku","connectfour","reversi","tanktrouble"})
                QVERIFY(!page.findChild<QPushButton*>("miniAppOpen_"+id)->isVisible());
            QTest::mouseClick(entry,Qt::LeftButton);
            for(const auto& id:QStringList{"gomoku","connectfour","reversi","tanktrouble"}) {
                auto* open=page.findChild<QPushButton*>("miniAppOpen_"+id);QVERIFY(open&&open->isVisible());
                QTest::mouseClick(open,Qt::LeftButton);
                auto* game=page.findChild<QWidget*>(id+"Page");QVERIFY(game&&game->isVisible());
                auto* back=page.findChild<QPushButton*>("miniGameBack_"+id);QVERIFY(back&&back->text()=="返回小游戏");
                QTest::mouseClick(back,Qt::LeftButton);QVERIFY(open->isVisible());
            }
            auto* favorite=page.findChild<QPushButton*>("miniAppFavorite_gomoku");
            if(!favorite->isChecked())QTest::mouseClick(favorite,Qt::LeftButton);
            Theme::instance().setMode(Theme::Light);capture(page,"games-entry-light");
            Theme::instance().setMode(Theme::Dark);capture(page,"games-entry-dark");
            page.resize(800,600);capture(page,"games-entry-minimum");Theme::instance().setMode(Theme::Light);
            QTest::mouseClick(page.findChild<QPushButton*>("miniAppBack_4"),Qt::LeftButton);
            QVERIFY(entry->isVisible());QVERIFY(!page.findChild<QPushButton*>("miniAppOpen_gomoku")->isVisible());
            page.openApp("web");QVERIFY(entry->isVisible());
            page.findChild<QComboBox*>("miniAppFilter")->setCurrentIndex(1);QVERIFY(entry->isVisible());
        }
        MiniAppsPage restored(&service);restored.show();
        restored.findChild<QComboBox*>("miniAppFilter")->setCurrentIndex(1);
        QVERIFY(restored.findChild<QPushButton*>("miniAppOpen_games")->isVisible());
        restored.openApp("games");QVERIFY(restored.findChild<QPushButton*>("miniAppFavorite_gomoku")->isChecked());
    }
    void navigationSearchAndAppearance() {
        MockService service;
        AppController controller(&service);
        MainWindow window(&controller);
        window.show();
        QTest::qWait(100);
        auto* nav = window.findChild<QPushButton*>("nav_7");
        QVERIFY(nav);
        QTest::mouseClick(nav, Qt::LeftButton);
        QCOMPARE(window.findChild<QStackedWidget*>("pages")->currentIndex(), 7);
        auto* page = window.findChild<MiniAppsPage*>();
        QVERIFY(page);
        auto* search = page->findChild<QLineEdit*>("miniAppSearch");
        search->setText("便签");
        QVERIFY(page->findChild<QWidget*>("miniAppCard_notes")->isVisible());
        QVERIFY(!page->findChild<QWidget*>("miniAppCard_todos")->isVisible());
        search->clear();
        page->findChild<QPushButton*>("miniAppFavorite_notes")->click();
        QVERIFY(page->findChild<QPushButton*>("miniAppFavorite_notes")->isChecked());
        auto* filter = page->findChild<QComboBox*>("miniAppFilter");
        filter->setCurrentIndex(1);
        QVERIFY(page->findChild<QWidget*>("miniAppCard_notes")->isVisible());
        QVERIFY(!page->findChild<QWidget*>("miniAppCard_calculator")->isVisible());
        filter->setCurrentIndex(0);
        capture(window, "center-light");
        window.resize(960, 640);
        QTest::qWait(80);
        for (auto* button : window.findChildren<QPushButton*>())
            if (button->property("pageIndex").isValid())
                QVERIFY(window.rect().contains(QRect(button->mapTo(&window, QPoint()), button->size())));
        capture(window, "center-minimum");
        Theme::instance().setMode(Theme::Dark);
        capture(window, "center-dark");
        Theme::instance().setSkin(Theme::PencilSkin);
        capture(window, "center-pencil-dark");
        Theme::instance().setMode(Theme::Light);
        capture(window, "center-pencil");
        page->openApp("calculator");
        page->findChild<QLineEdit*>("miniAppExpression")->setText("(12 + 8) / 4");
        page->findChild<QPushButton*>("miniAppCalculate")->click();
        capture(window, "calculator");
        QTest::mouseClick(page->findChild<QPushButton*>("miniAppBack_3"), Qt::LeftButton);
        QVERIFY(search->isVisible());
        QTest::mouseClick(window.findChild<QPushButton*>("nav_0"), Qt::LeftButton);
        QCOMPARE(window.findChild<QStackedWidget*>("pages")->currentIndex(), 0);
        Theme::instance().setSkin(Theme::DefaultSkin);
    }
};
QTEST_MAIN(MiniAppsTest)
#include "tst_miniapps.moc"
