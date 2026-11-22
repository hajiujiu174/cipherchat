#include "features/games/badmintonengine.h"
#include "features/games/gomokuwire.h"
#include "services/mockservice.h"
#include "features/miniapps/miniappspage.h"
#include "features/games/badmintonpage.h"
#include "ui/theme.h"
#include <QDir>
#include <QPushButton>
#include <QtTest>
using namespace Messager;
class BadmintonTest:public QObject {
    Q_OBJECT
private slots:
    // 验证“初始状态And输入CannotAdvance时间”场景中的关键结果，断言失败时由测试框架报告。
    void initialStateAndInputCannotAdvanceTime() {
        BadmintonEngine e;auto initial=e.snapshot();QVERIFY(BadmintonEngine::validSnapshot(initial));
        for(int n=0;n<1000;++n)e.input(0,2);
        QCOMPARE(e.snapshot(),initial);e.step();QCOMPARE(e.snapshot()["players"].toArray()[0].toArray()[0].toDouble(),94.5);
        e.input(-1,8);e.input(2,8);e.input(0,32);QVERIFY(BadmintonEngine::validSnapshot(e.snapshot()));
    }
    // 验证“移动跳跃AndCourtBounds”场景中的关键结果，断言失败时由测试框架报告。
    void movementJumpAndCourtBounds() {
        BadmintonEngine e;e.input(0,2);e.input(1,1);
        for(int n=0;n<150;++n)e.step();
        auto p=e.snapshot()["players"].toArray();QCOMPARE(p[0].toArray()[0].toDouble(),280.0);QCOMPARE(p[1].toArray()[0].toDouble(),548.0);
        e.input(0,4);e.step();QVERIFY(e.snapshot()["players"].toArray()[0].toArray()[1].toDouble()<522);
        for(int n=0;n<60;++n)e.step();QCOMPARE(e.snapshot()["players"].toArray()[0].toArray()[1].toDouble(),522.0);
        // Holding jump cannot retrigger until released.
        e.step();QCOMPARE(e.snapshot()["players"].toArray()[0].toArray()[1].toDouble(),522.0);
    }
    // 验证“发球AuthorityAndRacketCooldown”场景中的关键结果，断言失败时由测试框架报告。
    void serveAuthorityAndRacketCooldown() {
        BadmintonEngine e;e.input(1,8);e.step();QVERIFY(e.snapshot()["serving"].toBool());
        e.input(0,8);e.step();QVERIFY(!e.snapshot()["serving"].toBool());QCOMPARE(e.snapshot()["lastHit"].toInt(),0);
        for(int n=0;n<16;++n){e.input(0,0);e.input(0,8);e.step();}
        QCOMPARE(e.snapshot()["hits"].toInt(),1);QVERIFY(BadmintonEngine::validSnapshot(e.snapshot()));
    }
    // 验证“scoringAndFirstToSeven”场景中的关键结果，断言失败时由测试框架报告。
    void scoringAndFirstToSeven() {
        BadmintonEngine e;
        for(int n=0;n<4000&&!e.snapshot()["finished"].toBool();++n){e.input(0,0);if(e.snapshot()["serving"].toBool())e.input(0,8);e.step();QVERIFY(BadmintonEngine::validSnapshot(e.snapshot()));}
        QVERIFY(e.snapshot()["finished"].toBool());QCOMPARE(e.snapshot()["score"].toArray(),QJsonArray({7,0}));
        const QJsonValue score=e.snapshot()["score"];for(int n=0;n<100;++n)e.step();QCOMPARE(e.snapshot()["score"],score);
    }
    // 验证“computerReturnsAndLongRunStays有效”场景中的关键结果，断言失败时由测试框架报告。
    void computerReturnsAndLongRunStaysValid() {
        BadmintonEngine e;e.input(0,8);
        for(int n=0;n<700;++n){e.computerInput();e.step();QVERIFY(BadmintonEngine::validSnapshot(e.snapshot()));}
        QVERIFY(e.snapshot()["hits"].toInt()>=2);
    }
    // 验证“malformedSnapshots”场景中的关键结果，断言失败时由测试框架报告。
    void malformedSnapshots() {
        BadmintonEngine e;auto s=e.snapshot();s["players"]=QJsonArray{};QVERIFY(!BadmintonEngine::validSnapshot(s));
        s=e.snapshot();s["ball"]=QJsonArray{99999,0,0,0};QVERIFY(!BadmintonEngine::validSnapshot(s));
        s=e.snapshot();s["frame"]=0.5;QVERIFY(!BadmintonEngine::validSnapshot(s));
        s=e.snapshot();s["score"]=QJsonArray{7,7};s["finished"]=true;QVERIFY(!BadmintonEngine::validSnapshot(s));
        s=e.snapshot();s["finished"]=true;QVERIFY(!BadmintonEngine::validSnapshot(s));
    }
    // 验证“invite线上格式AndStandalone窗口”场景中的关键结果，断言失败时由测试框架报告。
    void inviteWireAndStandaloneWindow() {
        auto invite=QJsonObject{{"game","badminton"},{"type","invite"},{"room",QString(32,'a')},{"black","alice"},{"white","bob"},{"run",QString(32,'b')}};
        auto text=GomokuWire::encode(invite);QCOMPARE(GomokuWire::decode(text)["game"].toString(),QString("badminton"));
        invite["type"]="state";QVERIFY(GomokuWire::decode(GomokuWire::encode(invite)).isEmpty());
        MockService service;MiniAppsPage page(&service);page.show();page.openApp("games");
        QVERIFY(page.findChild<QPushButton*>("miniAppOpen_badminton")->isVisible());page.openApp("badminton");
        auto* game=page.findChild<BadmintonPage*>();QVERIFY(game&&game->isWindow()&&game->isFullScreen());
        auto* board=game->findChild<BadmintonBoard*>();QVERIFY(board);QTest::mouseClick(game->findChild<QPushButton*>("badmintonPractice"),Qt::LeftButton);
        QVERIFY(game->controller()->practice());QTest::keyClick(board,Qt::Key_Space);
        QTRY_VERIFY(game->controller()->state()["hits"].toInt()>0);
        const auto dir=qEnvironmentVariable("BADMINTON_EVIDENCE",QDir::tempPath()+"/badminton-evidence");QDir().mkpath(dir);
        for(auto mode:{Theme::Light,Theme::Dark}){Theme::instance().setMode(mode);QTest::qWait(50);QVERIFY(game->grab().save(dir+(mode==Theme::Light?"/badminton-light.png":"/badminton-dark.png")));}
        game->close();QVERIFY(!game->isVisible());QVERIFY(page.isVisible());
        page.openApp("badminton");QVERIFY(game->controller()->practice());QCOMPARE(page.findChildren<BadmintonPage*>().size(),1);
        game->controller()->leave();QVERIFY(game->controller()->room().isEmpty());game->close();
    }
};
// 登记 BadmintonTest 的 Qt 测试入口，由测试框架初始化环境并执行断言。
QTEST_MAIN(BadmintonTest)
#include "tst_badminton.moc"
