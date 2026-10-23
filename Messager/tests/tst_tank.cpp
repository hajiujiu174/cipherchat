#include "services/tankengine.h"
#include "services/gomokuwire.h"
#include "services/mockservice.h"
#include "ui/miniappspage.h"
#include "ui/tankpage.h"
#include "ui/theme.h"
#include <QJsonArray>
#include <QDir>
#include <QPushButton>
#include <QtTest>
#include <queue>
#include <set>
#include <cmath>
#include <limits>
using namespace Messager;
class TankTest:public QObject {
    Q_OBJECT
    static QJsonArray spawns(double x=30,double y=30,double angle=0) {return {QJsonArray{x,y,angle},QJsonArray{600,390,180}};}
    // A remote, short wall selects the custom arena path without obstructing test shots.
    static QJsonArray arena() {return {QJsonValue(QJsonArray{300,200,300,260})};}
private slots:
    void mazeConnected() {
        for(int n=0;n<12;++n) {
            TankEngine e;auto s=e.snapshot();QVERIFY(TankEngine::validSnapshot(s));
            std::set<std::pair<int,int>> blocked;
            for(auto v:s["walls"].toArray()) {
                auto a=v.toArray();int x=a[0].toInt(),y=a[1].toInt(),x2=a[2].toInt(),y2=a[3].toInt();
                if(x==x2&&x>0&&x<660) {int l=(y/60)*11+x/60-1;blocked.insert({l,l+1});}
                if(y==y2&&y>0&&y<420) {int t=(y/60-1)*11+x/60;blocked.insert({t,t+11});}
            }
            std::set<int> visited{0};std::queue<int> q;q.push(0);
            while(!q.empty()) {int p=q.front();q.pop();for(auto d:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                int x=p%11+d.first,y=p/11+d.second;if(x<0||x>=11||y<0||y>=7)continue;int to=y*11+x;
                if(blocked.count({qMin(p,to),qMax(p,to)})||visited.count(to))continue;visited.insert(to);q.push(to);
            }}
            QCOMPARE(visited.size(),size_t(77));
        }
    }
    void wallStopsTank() {
        TankEngine e;e.reset(QJsonArray{QJsonValue(QJsonArray{60,0,60,60})},spawns());e.input(0,1);
        for(int i=0;i<80;++i)e.step();auto a=e.snapshot()["tanks"].toArray()[0].toArray();
        QVERIFY(a[0].toDouble()<45);QVERIFY(a[0].toDouble()>30);QCOMPARE(a[1].toDouble(),30.0);
    }
    void bounceAndSelfHit() {
        TankEngine e;e.reset(arena(),spawns(610,30,0));e.input(0,16);e.step();e.input(0,0);
        bool bounced=false,dead=false;
        for(int i=0;i<90;++i) {
            e.step();auto s=e.snapshot();for(auto v:s["shots"].toArray())if(v.toArray()[4].toInt()<10)bounced=true;
            if(!s["tanks"].toArray()[0].toArray()[3].toBool()) {dead=true;QCOMPARE(s["score"].toArray()[1].toInt(),1);break;}
        }
        QVERIFY(bounced);QVERIFY(dead);
    }
    void hitOpponentAndNextRound() {
        TankEngine e;e.reset(arena(),{QJsonArray{30,30,0},QJsonArray{150,30,180}});e.input(0,16);e.step();e.input(0,0);
        for(int i=0;i<45 && !e.snapshot()["wait"].toInt();++i)e.step();
        auto s=e.snapshot();QCOMPARE(s["score"].toArray()[0].toInt(),1);QVERIFY(!s["tanks"].toArray()[1].toArray()[3].toBool());
        for(int i=0;i<100;++i)e.step();s=e.snapshot();QCOMPARE(s["round"].toInt(),2);QVERIFY(s["tanks"].toArray()[1].toArray()[3].toBool());QCOMPARE(s["score"].toArray()[0].toInt(),1);
    }
    void simultaneousDeathsDraw() {
        TankEngine e;e.reset(arena(),{QJsonArray{30,30,0},QJsonArray{150,30,180}});e.input(0,16);e.input(1,16);e.step();e.input(0,0);e.input(1,0);
        for(int i=0;i<60&&!e.snapshot()["wait"].toInt();++i)e.step();auto s=e.snapshot();
        QCOMPARE(s["score"].toArray(),QJsonArray({0,0}));QVERIFY(!s["tanks"].toArray()[0].toArray()[3].toBool());QVERIFY(!s["tanks"].toArray()[1].toArray()[3].toBool());
    }
    void fivePointsFinish() {
        TankEngine e;e.reset(arena(),{QJsonArray{30,30,0},QJsonArray{150,30,180}});
        for(int round=0;round<5;++round) {
            e.input(0,16);e.step();e.input(0,0);for(int i=0;i<60&&!e.snapshot()["wait"].toInt()&&!e.snapshot()["finished"].toBool();++i)e.step();
            if(round<4)for(int i=0;i<100;++i)e.step();
        }
        auto s=e.snapshot();QVERIFY(s["finished"].toBool());QCOMPARE(s["score"].toArray()[0].toInt(),5);
        e.input(0,31);for(int i=0;i<20;++i)e.step();QCOMPARE(e.snapshot()["tanks"],s["tanks"]);
    }
    void ammoAndExpiry() {
        TankEngine e;e.reset(arena(),spawns(30,30,0));e.input(0,16);
        for(int i=0;i<60;++i)e.step();QVERIFY(e.snapshot()["shots"].toArray().size()<=5);e.input(0,0);
        for(int i=0;i<500;++i)e.step();QVERIFY(e.snapshot()["shots"].toArray().isEmpty());
        QCOMPARE(e.snapshot()["tanks"].toArray()[0].toArray()[4].toInt(),5);
    }
    void pointerSpeedAndNoTeleport() {
        TankEngine e;e.reset(arena(),spawns());
        const auto before=e.snapshot();
        for(int i=0;i<1000;++i)QVERIFY(e.pointerInput(0,QPointF(600,30),false));
        QCOMPARE(e.snapshot(),before); // Input events do not advance simulation.
        QPointF previous(30,30);double travelled=0;
        for(int i=0;i<50;++i) {
            e.step();const auto tank=e.snapshot()["tanks"].toArray()[0].toArray();QPointF next(tank[0].toDouble(),tank[1].toDouble());
            const double delta=std::hypot(next.x()-previous.x(),next.y()-previous.y());
            QVERIFY(delta<=TankEngine::MaxSpeed*TankEngine::StepSeconds+1e-8);travelled+=delta;previous=next;
        }
        QVERIFY(std::abs(travelled-100)<1e-7);QVERIFY(std::abs(previous.x()-130)<1e-7);
        e.reset(arena(),spawns(30,30,315));QVERIFY(e.pointerInput(0,QPointF(200,200),false));e.step();
        auto diagonal=e.snapshot()["tanks"].toArray()[0].toArray();
        QVERIFY(std::abs(std::hypot(diagonal[0].toDouble()-30,diagonal[1].toDouble()-30)-2)<1e-8);
    }
    void pointerStopsAndTurns() {
        TankEngine e;e.reset(arena(),spawns());QVERIFY(e.pointerInput(0,QPointF(31,30),false));e.step();
        auto tank=e.snapshot()["tanks"].toArray()[0].toArray();QCOMPARE(tank[0].toDouble(),30.0);
        for(int i=0;i<30;++i)e.step();QCOMPARE(e.snapshot()["tanks"].toArray()[0],QJsonValue(tank));
        e.reset(arena(),spawns());e.pointerInput(0,QPointF(30,50),false);e.step();tank=e.snapshot()["tanks"].toArray()[0].toArray();
        QCOMPARE(tank[0].toDouble(),30.0);QCOMPARE(tank[1].toDouble(),30.0);QCOMPARE(tank[2].toDouble(),357.0);
        e.input(0,0);const QJsonValue stopped=e.snapshot()["tanks"];
        for(int i=0;i<30;++i)e.step();QCOMPARE(e.snapshot()["tanks"],stopped);
    }
    void pointerCannotCrossWalls() {
        TankEngine e;e.reset(QJsonArray{QJsonValue(QJsonArray{60,0,60,60})},spawns());
        for(int i=0;i<100;++i) {e.pointerInput(0,QPointF(600,30),false);e.step();}
        const auto tank=e.snapshot()["tanks"].toArray()[0].toArray();QVERIFY(tank[0].toDouble()<45);QVERIFY(tank[0].toDouble()>30);
    }
    void aimOnlyCircleAndBoundary() {
        TankEngine e;e.reset(arena(),spawns(100,100,0));
        for(auto target:{QPointF(100,120),QPointF(80,100),QPointF(100,64),QPointF(100,100)}){
            e.pointerInput(0,target,false);for(int n=0;n<60;++n)e.step();
            auto t=e.snapshot()["tanks"].toArray()[0].toArray();QCOMPARE(t[0].toDouble(),100.0);QCOMPARE(t[1].toDouble(),100.0);
        }
        e.pointerInput(0,QPointF(160,100),false);for(int n=0;n<50;++n)e.step();
        auto t=e.snapshot()["tanks"].toArray()[0].toArray();QCOMPARE(t[0].toDouble(),124.0);QCOMPARE(t[1].toDouble(),100.0);
        QCOMPARE(e.snapshot()["aim"].toArray()[0].toDouble(),0.0);
    }
    void turretAimsAndFiresWhenBodyCannotTurn() {
        TankEngine e;e.reset(QJsonArray{QJsonArray{60,0,60,80}},spawns(44,30,0));
        e.pointerInput(0,QPointF(44,50),true);e.step();const auto s=e.snapshot();
        const auto t=s["tanks"].toArray()[0].toArray();QCOMPARE(t[0].toDouble(),44.0);QCOMPARE(t[1].toDouble(),30.0);
        QCOMPARE(t[2].toDouble(),0.0);QCOMPARE(s["aim"].toArray()[0].toDouble(),270.0);
        QCOMPARE(s["shots"].toArray().size(),1);QCOMPARE(s["shots"].toArray()[0].toArray()[2].toDouble(),270.0);
    }
    void solidBodySlidesAndEscapesWall() {
        TankEngine e;e.reset(QJsonArray{QJsonArray{60,0,60,180}},spawns(44,30,0));
        e.pointerInput(0,QPointF(200,160),false);for(int n=0;n<25;++n)e.step();
        auto t=e.snapshot()["tanks"].toArray()[0].toArray();QVERIFY(t[0].toDouble()<=44.00001);QVERIFY(t[1].toDouble()>40);
        const auto x=t[0].toDouble(),y=t[1].toDouble();e.pointerInput(0,QPointF(0,y),false);e.step();
        t=e.snapshot()["tanks"].toArray()[0].toArray();QVERIFY(t[0].toDouble()<x);QVERIFY(std::abs(t[0].toDouble()-x)<=2.00001);
        // A short wall aligned with the old visual track gap is also solid.
        e.reset(QJsonArray{QJsonArray{60,29,60,31}},spawns());e.pointerInput(0,QPointF(200,30),false);
        for(int n=0;n<100;++n)e.step();QVERIFY(e.snapshot()["tanks"].toArray()[0].toArray()[0].toDouble()<=44.00001);
    }
    void barrelCannotSpawnShotThroughWall() {
        TankEngine e;e.reset(QJsonArray{QJsonArray{60,0,60,80}},spawns(44,30,0));
        e.pointerInput(0,QPointF(64,30),true);e.step();const auto s=e.snapshot();
        for(auto shot:s["shots"].toArray())QVERIFY(shot.toArray()[0].toDouble()<60);
        QVERIFY(!s["tanks"].toArray()[0].toArray()[3].toBool()||!s["shots"].toArray().isEmpty());
    }
    void mousePredictionMatchesHostIncludingAim() {
        TankEngine host,predictor;host.reset(arena(),spawns(100,100,0));QVERIFY(predictor.loadMovementSnapshot(host.snapshot()));
        for(int n=0;n<100;++n){const QPointF target=n<50?QPointF(100,120):QPointF(200,200);
            host.pointerInput(0,target,false);predictor.pointerInput(0,target,false);host.step();predictor.predictMovement(0);
            QCOMPARE(host.snapshot()["tanks"],predictor.snapshot()["tanks"]);QCOMPARE(host.snapshot()["aim"],predictor.snapshot()["aim"]);}
        auto malformed=host.snapshot();malformed["aim"]=QJsonArray{std::numeric_limits<double>::quiet_NaN(),0};QVERIFY(!TankEngine::validSnapshot(malformed));
    }
    void pointerRejectsInvalidCoordinates() {
        TankEngine e;const auto before=e.snapshot();
        for(auto target:{QPointF(-1,0),QPointF(661,30),QPointF(30,421),QPointF(std::numeric_limits<double>::infinity(),0),QPointF(0,std::numeric_limits<double>::quiet_NaN())})
            QVERIFY(!e.pointerInput(0,target,true));
        QVERIFY(!e.pointerInput(2,QPointF(30,30),false));QCOMPARE(e.snapshot(),before);
    }
    void pointerFireKeepsCooldownAndAmmoLimit() {
        TankEngine e;e.reset(arena(),spawns());
        for(int i=0;i<15;++i) {for(int spam=0;spam<50;++spam)e.pointerInput(0,QPointF(30,30),true);e.step();}
        QCOMPARE(e.snapshot()["shots"].toArray().size(),1);e.step();QCOMPARE(e.snapshot()["shots"].toArray().size(),2);
        for(int i=16;i<80;++i) {e.pointerInput(0,QPointF(30,30),true);e.step();}
        QCOMPARE(e.snapshot()["shots"].toArray().size(),5);QCOMPARE(e.snapshot()["tanks"].toArray()[0].toArray()[4].toInt(),0);
    }
    void rejectsMalformedSnapshots() {
        TankEngine e;auto s=e.snapshot();QVERIFY(TankEngine::validSnapshot(s));
        s["frame"]=-1;QVERIFY(!TankEngine::validSnapshot(s));s=e.snapshot();s["tanks"]=QJsonArray{};QVERIFY(!TankEngine::validSnapshot(s));
        s=e.snapshot();auto a=s["tanks"].toArray();a[0]=QJsonArray{900,30,0,true,5};s["tanks"]=a;QVERIFY(!TankEngine::validSnapshot(s));
    }
    void wireAndCatalog() {
        auto invite=QJsonObject{{"game","tanktrouble"},{"type","invite"},{"room",QString(32,'a')},{"black","alice"},{"white","bob"},{"run",QString(32,'b')}};
        auto text=GomokuWire::encode(invite);QCOMPARE(GomokuWire::decode(text)["game"].toString(),QString("tanktrouble"));QVERIFY(!GomokuWire::isControl(text));
        invite["type"]="state";QVERIFY(GomokuWire::decode(GomokuWire::encode(invite)).isEmpty());
        MockService service;MiniAppsPage page(&service);page.resize(1050,820);page.show();page.openApp("tanktrouble");
        auto* tank=page.findChild<TankPage*>();QVERIFY(tank&&tank->isVisible());
        QVERIFY(tank->isWindow());QVERIFY(tank->isFullScreen());QVERIFY(tank->window()!=page.window());
        auto* board=tank->findChild<TankBoard*>();QVERIFY(board);QCOMPARE(board->window(),tank);
        QVERIFY(board->maximumHeight()>520);
        const auto dir=qEnvironmentVariable("TANK_EVIDENCE",QDir::tempPath()+"/tank-evidence");QDir().mkpath(dir);
        for(auto mode:{Theme::Light,Theme::Dark}) {Theme::instance().setMode(mode);QTest::qWait(50);QVERIFY(tank->grab().save(dir+(mode==Theme::Light?"/tank-light.png":"/tank-dark.png")));}
        auto* toggle=tank->findChild<QPushButton*>("tankFullScreen");QVERIFY(toggle);
        QTest::mouseClick(toggle,Qt::LeftButton);QVERIFY(!tank->isFullScreen());
        tank->resize(1600,1100);QTest::qWait(30);QVERIFY(board->height()>520);
        QTest::mouseClick(toggle,Qt::LeftButton);QVERIFY(tank->isFullScreen());
        QApplication::setActiveWindow(tank);board->setFocus();QTest::qWait(30);
        QTest::keyClick(board,Qt::Key_Escape);QVERIFY(!tank->isFullScreen());
        QTest::keyClick(board,Qt::Key_F11);QVERIFY(tank->isFullScreen());
        QTest::mouseClick(tank->findChild<QPushButton*>("tankWindowBack"),Qt::LeftButton);
        QVERIFY(!tank->isVisible());QVERIFY(page.isVisible());
        page.openApp("tanktrouble");QVERIFY(tank->isVisible()&&tank->isFullScreen());
        QCOMPARE(page.findChildren<TankPage*>().size(),1);tank->close();QVERIFY(!tank->isVisible());
    }
};
QTEST_MAIN(TankTest)
#include "tst_tank.moc"

