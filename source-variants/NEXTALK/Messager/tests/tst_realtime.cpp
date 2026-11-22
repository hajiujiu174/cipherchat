#include "services/chatservice.h"
#include "services/tankcontroller.h"
#include "services/badmintoncontroller.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
using namespace Messager;
class LinkService:public ChatService {
public:
    QString name;LinkService* other=nullptr;int delay=60,jitter=0,lossEvery=0,packets=0,dropped=0;QString invitation;bool connected=true,dropNextInput=false;
    QVector<Conversation> cs;QVector<Transfer> ts;
    explicit LinkService(QString user):name(user){}
    const QVector<Conversation>& conversations()const override{return cs;}
    QVector<Message> messages(const QString&)const override{return {};}
    const QVector<Transfer>& transfers()const override{return ts;}
    Conversation conversation(const QString&)const override{return {};}
    QString send(const QString&,const QString& text,bool=false)override{invitation=text;return "invite";}
    void retryMessage(const QString&)override{}void markViewed(const QString&)override{}void simulateIncoming(const QString&)override{}
    void addTransfer(const QString&,qint64,const QString&)override{}void sendFile(const QString&,const QString&)override{}void transferAction(const QString&,const QString&)override{}
    QString openDirectConversation(const QString& peer)override{return "peer:"+peer;}
    bool isDemo()const override{return false;}bool online()const override{return connected;}bool gameRelaySupported()const override{return true;}QString username()const override{return name;}
    QVector<Contact> contacts()const override{Contact c;c.username=other->name;c.friendContact=true;c.online=true;return {c};}
    bool sendGamePacket(const QString&,const QJsonObject& p)override{
        if(dropNextInput&&p["type"]=="input"){dropNextInput=false;++packets;++dropped;return true;}
        ++packets;if(lossEvery&&packets%lossEvery==0&&p["type"]!="join"&&p["type"]!="leave"){++dropped;return true;}
        auto* destination=other;auto from=name;const int d=qMax(0,delay+(jitter?(packets%3-1)*jitter:0));
        QTimer::singleShot(d,destination,[destination,from,p]{emit destination->gamePacket(from,p);});return true;
    }
};
template<class C>QJsonObject view(C& c){
#ifdef LATENCY_BASELINE
    return c.state();
#else
    return c.predictedState();
#endif
}
class RealtimeTest:public QObject {
    Q_OBJECT
    QJsonArray records;
private slots:
    void predictionUsesAuthoritativeMovementRules(){
#ifndef LATENCY_BASELINE
        TankEngine host,predictor;QVERIFY(predictor.loadMovementSnapshot(host.snapshot()));
        for(int n=0;n<40;++n){host.input(1,1);predictor.input(1,1);host.step();predictor.predictMovement(1);QCOMPARE(host.snapshot()["tanks"].toArray()[1].toArray(),predictor.snapshot()["tanks"].toArray()[1].toArray());}
        BadmintonEngine original,copy;QVERIFY(copy.loadSnapshot(original.snapshot()));
        for(int n=0;n<60;++n){int buttons=n==0?5:1;original.input(1,buttons);copy.input(1,buttons);original.step();copy.step();QCOMPARE(original.snapshot(),copy.snapshot());}
#endif
    }
    void historyIsBoundedAndAckIsValidated(){
#ifndef LATENCY_BASELINE
        RealtimeSync sync;for(int n=0;n<10000;++n)sync.record({{"buttons",1}});QCOMPARE(sync.pending.size(),32);sync.sent();QVERIFY(!sync.validAck(10001));QVERIFY(!sync.validAck(-1));QVERIFY(!sync.validAck(0.5));
        sync.snapshot(200,9990,50);QCOMPARE(sync.pending.size(),10);QVERIFY(!sync.validAck(9989));sync.snapshot(240,10000,100);QVERIFY(sync.pending.isEmpty());QVERIFY(sync.interpolationMs()>=25&&sync.interpolationMs()<=80);QCOMPARE(sync.rttMs(),140);
#endif
    }
    void lostJumpActionIsRetriedOnce(){
#ifndef LATENCY_BASELINE
        LinkService a("host"),b("guest");a.other=&b;b.other=&a;a.delay=b.delay=30;BadmintonController host(&a),guest(&b);QVERIFY(host.create("guest"));Message m;m.sender="host";m.text=a.invitation;QVERIFY(guest.openInvitation(m));QTRY_VERIFY_WITH_TIMEOUT(host.active()&&guest.active(),3000);
        b.dropNextInput=true;guest.setButtons(4);guest.setButtons(0);QTRY_VERIFY_WITH_TIMEOUT(host.state()["players"].toArray()[1].toArray()[1].toDouble()<520,800);QCOMPARE(b.dropped,1);
        QTest::qWait(800);QCOMPARE(host.state()["players"].toArray()[1].toArray()[1].toDouble(),522.0);QCOMPARE(host.state()["hits"].toInt(),0);
#endif
    }
    void response_data(){QTest::addColumn<QString>("game");QTest::addColumn<int>("delay");QTest::addColumn<int>("jitter");QTest::addColumn<int>("loss");for(auto game:{QString("tank"),QString("badminton")}){QTest::newRow(qPrintable(game+"-120ms"))<<game<<60<<0<<0;QTest::newRow(qPrintable(game+"-jitter-loss"))<<game<<60<<15<<33;}}
    void response(){QFETCH(QString,game);QFETCH(int,delay);QFETCH(int,jitter);QFETCH(int,loss);
        LinkService a("host"),b("guest");a.other=&b;b.other=&a;a.delay=b.delay=delay;a.jitter=b.jitter=jitter;a.lossEvery=b.lossEvery=loss;
        auto exercise=[&](auto& host,auto& guest){
            QVERIFY(host.create("guest"));Message m;m.sender="host";m.text=a.invitation;QVERIFY(guest.openInvitation(m));QTRY_VERIFY_WITH_TIMEOUT(guest.active()&&host.active(),3500);
            QTest::qWait(180);auto position=[&](bool predicted){auto s=predicted?view(guest):guest.state();return game=="tank"?s["tanks"].toArray()[1].toArray()[0].toDouble():s["players"].toArray()[1].toArray()[0].toDouble();};
            const double start=position(true);QElapsedTimer watch;watch.start();guest.setButtons(1);
            while(std::abs(position(true)-start)<1.5&&watch.elapsed()<1000)QTest::qWait(1);
            const auto feedback=watch.elapsed();QVERIFY(std::abs(position(true)-start)>=1.5);
#ifndef LATENCY_BASELINE
            QVERIFY2(feedback<=65,qPrintable(QString("local feedback %1 ms").arg(feedback)));
#endif
            guest.setButtons(0);QTest::qWait(loss?1700:700);auto gs=guest.state();auto hs=host.state();auto key=game=="tank"?"tanks":"players";
            const auto gp=gs[key].toArray()[1].toArray(),hp=hs[key].toArray()[1].toArray();QVERIFY(std::abs(gp[0].toDouble()-hp[0].toDouble())<0.01);
#ifndef LATENCY_BASELINE
            if(loss)QVERIFY(a.dropped+b.dropped>0);
            QVERIFY(guest.networkStats()["pending_inputs"].toInt()<=32);QVERIFY(guest.networkStats()["rtt_ms"].toInt()>0);
#endif
            records.append(QJsonObject{{"game",game},{"injected_one_way_ms",delay},{"jitter_ms",jitter},{"drop_every_nth_packet",loss},{"packets_dropped",a.dropped+b.dropped},{"local_feedback_ms",feedback},{"host_packets",a.packets},{"guest_packets",b.packets},{"converged",true}});
        };
        if(game=="tank"){TankController host(&a),guest(&b);exercise(host,guest);}else{BadmintonController host(&a),guest(&b);exercise(host,guest);}
    }
    void cleanupTestCase(){const auto file=qEnvironmentVariable("REALTIME_RESULTS");if(!file.isEmpty()){QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(records).toJson());}}
};
QTEST_MAIN(RealtimeTest)
#include "tst_realtime.moc"
