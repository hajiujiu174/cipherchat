#include "services/casualgames.h"
#include "services/mockservice.h"
#include "ui/casualgamepage.h"
#include "ui/miniappspage.h"
#include "ui/theme.h"
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>
#include <QFocusEvent>
#include <QtTest>
#include <cmath>
using namespace Messager;
class CasualTest:public QObject {
    Q_OBJECT
    QTemporaryDir settings;QString evidence;
    static QVector<int> inventory(const SheepGame& game){
        QVector<int> counts(8);for(const auto& t:game.tiles())if(!t.removed)++counts[t.kind];
        for(int id:game.tray())++counts[game.tiles()[id].kind];for(int id:game.reserve())++counts[game.tiles()[id].kind];return counts;
    }
private slots:
    void initTestCase(){
        QCoreApplication::setOrganizationName("MessagerCasualTests");QCoreApplication::setApplicationName("Casual");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        evidence=qEnvironmentVariable("CASUAL_EVIDENCE",settings.path()+"/evidence");QVERIFY(QDir().mkpath(evidence));
        Theme::instance().apply();
    }
    void jumpCentersAndProgression(){
        for(quint32 seed=0;seed<40;++seed){JumpGame g;g.reset(seed);
            for(int n=0;n<20;++n){const auto target=g.next().center;const int held=qRound(g.distance()/JumpGame::ChargeSpeed);
                QVERIFY(g.jump(held));QVERIFY(!g.lost());QVERIFY(g.perfect());QCOMPARE(g.score(),(n+1)*2);QCOMPARE(g.current().center,target);
                QVERIFY(std::hypot((g.pawn()-target).x(),(g.pawn()-target).y())<=.15);}
        }
    }
    void jumpMissResetAndBounds(){
        JumpGame g;g.reset(19);const auto pawn=g.pawn();QVERIFY(!g.jump(-1));QCOMPARE(g.pawn(),pawn);
        QVERIFY(g.jump(0));QVERIFY(!g.lost());QCOMPARE(g.score(),0);
        QVERIFY(g.jump(100000));QVERIFY(g.lost());const auto lost=g.pawn();QVERIFY(!g.jump(600));QCOMPARE(g.pawn(),lost);
        g.reset(19);QVERIFY(!g.lost());QCOMPARE(g.score(),0);QCOMPARE(g.pawn(),QPointF(0,0));
    }
    void generatedBoardsHaveCompleteSolutions(){
        for(int level:{1,2})for(quint32 seed=0;seed<50;++seed){SheepGame g;g.reset(level,seed);QCOMPARE(g.tiles().size(),level==1?27:90);
            const auto order=g.solution();const auto counts=inventory(g);for(int n:counts)QCOMPARE(n%3,0);
            for(int id:order){QVERIFY(g.exposed(id));QVERIFY(g.pick(id));QVERIFY(!g.lost());QVERIFY(g.tray().size()<=2);}
            QVERIFY(g.won());QCOMPARE(g.remaining(),0);QVERIFY(g.tray().isEmpty());QVERIFY(!g.pick(order.last()));}
    }
    void coverageAndTripleUndo(){
        SheepGame g;g.reset(1,31);QVERIFY(!g.exposed(0));QVERIFY(!g.pick(0));QVERIFY(!g.pick(-1));QVERIFY(!g.pick(999));
        const auto order=g.solution();QVERIFY(g.pick(order[0]));QVERIFY(!g.pick(order[0]));QVERIFY(g.pick(order[1]));QCOMPARE(g.tray().size(),2);
        QVERIFY(g.pick(order[2]));QCOMPARE(g.tray().size(),0);QCOMPARE(g.remaining(),24);
        QVERIFY(g.undo());QCOMPARE(g.tray().size(),2);QVERIFY(g.exposed(order[2]));QCOMPARE(g.remaining(),27);QCOMPARE(g.moves(),2);
        QVERIFY(!g.undo());QVERIFY(g.pick(order[2]));QCOMPARE(g.remaining(),24);QVERIFY(!g.undo());
    }
    void trayLimitToolsAndInventory(){
        bool found=false;
        for(quint32 seed=0;seed<100&&!found;++seed){SheepGame g;g.reset(2,seed);
            for(int step=0;step<30&&!g.lost();++step){int chosen=-1;
                for(const auto& tile:g.tiles())if(g.exposed(tile.id)){int inTray=0;for(int id:g.tray())if(g.tiles()[id].kind==tile.kind)++inTray;if(inTray<2){chosen=tile.id;break;}}
                if(chosen<0)break;QVERIFY(g.pick(chosen));}
            if(!g.lost())continue;found=true;QCOMPARE(g.tray().size(),7);QVERIFY(g.canReserve());const auto before=inventory(g);
            QVERIFY(g.moveToReserve());QVERIFY(!g.lost());QCOMPARE(g.tray().size(),4);QCOMPARE(g.reserve().size(),3);QCOMPARE(inventory(g),before);QVERIFY(!g.moveToReserve());
            const int remaining=g.remaining();QVERIFY(g.canShuffle());QVERIFY(g.shuffle());QVERIFY(!g.shuffle());QCOMPARE(inventory(g),before);QCOMPARE(g.remaining(),remaining);
            QVERIFY(!g.takeReserve(-1));QVERIFY(g.takeReserve(0));QCOMPARE(g.reserve().size(),2);QVERIFY(g.tray().size()<=5);
        }
        QVERIFY(found);
    }
    void inputLifecycleAndAccountScores(){
        CasualGamePage page("jumpjump");page.setScope("alice");page.openWindow();QTest::qWait(80);
        auto* board=page.findChild<CasualBoard*>();QVERIFY(board);board->setFocus();
        const int held=qRound(board->jumpGame().distance()/JumpGame::ChargeSpeed);
        QTest::keyPress(board,Qt::Key_Space);QVERIFY(board->charging());QTest::qWait(held);QTest::keyRelease(board,Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(board->jumpGame().score()>0,1000);const int best=page.bestScore();QVERIFY(best>0);QTest::qWait(500);
        QTest::keyPress(board,Qt::Key_Space);QVERIFY(board->charging());const auto beforeCancel=board->jumpGame().pawn();
        QFocusEvent focusOut(QEvent::FocusOut);QApplication::sendEvent(board,&focusOut);QVERIFY(!board->charging());QCOMPARE(board->jumpGame().pawn(),beforeCancel);
        page.close();QVERIFY(!board->charging());
        page.setScope("bob");QCOMPARE(page.bestScore(),0);QCOMPARE(board->jumpGame().score(),0);
        page.setScope("alice");QCOMPARE(page.bestScore(),best);QCOMPARE(board->jumpGame().score(),0);
        page.openWindow();QApplication::setActiveWindow(&page);board->setFocus();QTest::qWait(40);
        QTest::keyPress(board,Qt::Key_Space);QVERIFY(board->charging());QTest::keyClick(board,Qt::Key_Escape);
        QTRY_VERIFY(!page.isFullScreen());QVERIFY(!board->charging());page.close();
    }
    void catalogWindowsTilesAndScreenshots(){
        MockService service;MiniAppsPage catalog(&service);catalog.resize(1100,850);catalog.show();catalog.openApp("games");
        QVERIFY(catalog.findChild<QPushButton*>("miniAppOpen_jumpjump"));QVERIFY(catalog.findChild<QPushButton*>("miniAppOpen_sheep"));
        for(const auto& id:QStringList{"jumpjump","sheep"}){
            catalog.openApp(id);auto* page=catalog.findChild<CasualGamePage*>(id+"Page");QVERIFY(page&&page->isVisible());QVERIFY(page->isWindow());QVERIFY(page->isFullScreen());
            auto* board=page->findChild<CasualBoard*>();QVERIFY(board);
            if(id=="sheep"){
                int chosen=board->sheepGame().solution()[0];const auto rect=board->sheepGame().tiles()[chosen].rect;
                QTest::mouseClick(board,Qt::LeftButton,Qt::NoModifier,board->screenPoint(rect.center()));QCOMPARE(board->sheepGame().tray().size(),1);
                QTest::mouseClick(page->findChild<QPushButton*>("sheepUndo"),Qt::LeftButton);QCOMPARE(board->sheepGame().tray().size(),0);
            }
            for(auto mode:{Theme::Light,Theme::Dark}){Theme::instance().setMode(mode);QTest::qWait(70);QVERIFY(page->grab().save(evidence+"/"+id+(mode==Theme::Light?"-light.png":"-dark.png")));}
            if(id=="sheep"){
                const auto order=board->sheepGame().solution();for(int chosen:order){const auto center=board->sheepGame().tiles()[chosen].rect.center();QTest::mouseClick(board,Qt::LeftButton,Qt::NoModifier,board->screenPoint(center));}
                QVERIFY(board->sheepGame().won());QVERIFY(page->findChild<QPushButton*>("sheepNext")->isEnabled());
                QVERIFY(page->grab().save(evidence+"/sheep-level-complete.png"));page->findChild<QPushButton*>("sheepNext")->click();QCOMPARE(board->sheepGame().level(),2);
            }
            page->findChild<QPushButton*>(id+"FullScreen")->click();QVERIFY(!page->isFullScreen());page->resize(800,600);QTest::qWait(60);QVERIFY(page->grab().save(evidence+"/"+id+"-minimum.png"));
            if(id=="sheep"){page->findChild<QComboBox*>("sheepLevel")->setCurrentIndex(1);QCOMPARE(board->sheepGame().tiles().size(),90);QVERIFY(page->grab().save(evidence+"/sheep-challenge.png"));}
            QTest::mouseClick(page->findChild<QPushButton*>(id+"Back"),Qt::LeftButton);QVERIFY(!page->isVisible());QVERIFY(catalog.findChild<QPushButton*>("miniAppOpen_"+id)->isVisible());
            catalog.openApp(id);QCOMPARE(catalog.findChildren<CasualGamePage*>(id+"Page").size(),1);page->close();
        }
        Theme::instance().setMode(Theme::Light);QVERIFY(catalog.grab().save(evidence+"/nine-games-entry.png"));
    }
};
QTEST_MAIN(CasualTest)
#include "tst_casual.moc"
