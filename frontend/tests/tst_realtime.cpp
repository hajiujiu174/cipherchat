#include "services/chatservice.h"
#include "features/games/tankcontroller.h"
#include "features/games/badmintoncontroller.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
using namespace Messager;
class LinkService:public ChatService {
public:
    QString name;LinkService* other=nullptr;int delay=60,jitter=0,lossEvery=0,packets=0,dropped=0;QString invitation;bool connected=true,dropNextInput=false;
    QVector<Conversation> cs;QVector<Transfer> ts;
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit LinkService(QString user):name(user){}
    // 返回测试使用的会话列表。
    const QVector<Conversation>& conversations()const override{return cs;}
    // 返回测试使用的消息。
    QVector<Message> messages(const QString&)const override{return {};}
    // 返回测试使用的传输任务列表。
    const QVector<Transfer>& transfers()const override{return ts;}
    // 返回测试使用的会话。
    Conversation conversation(const QString&)const override{return {};}
    // 发送测试。
    QString send(const QString&,const QString& text,bool=false)override{invitation=text;return "invite";}
    // 重试消息。
    void retryMessage(const QString&)override{}/* 标记已查看。 */ void markViewed(const QString&)override{}/* 模拟收到的。 */ void simulateIncoming(const QString&)override{}
    // 添加传输。
    void addTransfer(const QString&,qint64,const QString&)override{}/* 发送文件。 */ void sendFile(const QString&,const QString&)override{}/* 传输操作。 */ void transferAction(const QString&,const QString&)override{}
    // 打开私聊会话。
    QString openDirectConversation(const QString& peer)override{return "peer:"+peer;}
    // 判断演示条件是否成立。
    bool isDemo()const override{return false;}/* 判断测试的是否已在线。 */ bool online()const override{return connected;}/* 返回测试使用的游戏中转受支持。 */ bool gameRelaySupported()const override{return true;}/* 返回测试使用的用户名。 */ QString username()const override{return name;}
    // 返回测试使用的联系人。
    QVector<Contact> contacts()const override{Contact c;c.username=other->name;c.friendContact=true;c.online=true;return {c};}
    // 发送游戏数据包。
    bool sendGamePacket(const QString&,const QJsonObject& p)override{
        if(dropNextInput&&p["type"]=="input"){dropNextInput=false;++packets;++dropped;return true;}
        ++packets;if(lossEvery&&packets%lossEvery==0&&p["type"]!="join"&&p["type"]!="leave"){++dropped;return true;}
        auto* destination=other;auto from=name;const int d=qMax(0,delay+(jitter?(packets%3-1)*jitter:0));
        QTimer::singleShot(d,destination,[destination,from,p]{emit destination->gamePacket(from,p);});return true;
    }
};
// 返回测试使用的视图。
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
    // 验证“预测UsesAuthoritative移动Rules”场景中的关键结果，断言失败时由测试框架报告。
    void predictionUsesAuthoritativeMovementRules(){
#ifndef LATENCY_BASELINE
        TankEngine host,predictor;QVERIFY(predictor.loadMovementSnapshot(host.snapshot()));
        for(int n=0;n<40;++n){host.input(1,1);predictor.input(1,1);host.step();predictor.predictMovement(1);QCOMPARE(host.snapshot()["tanks"].toArray()[1].toArray(),predictor.snapshot()["tanks"].toArray()[1].toArray());}
        BadmintonEngine original,copy;QVERIFY(copy.loadSnapshot(original.snapshot()));
        for(int n=0;n<60;++n){int buttons=n==0?5:1;original.input(1,buttons);copy.input(1,buttons);original.step();copy.step();QCOMPARE(original.snapshot(),copy.snapshot());}
#endif
    }
    // 验证“历史记录IsBoundedAnd接收确认IsValidated”场景中的关键结果，断言失败时由测试框架报告。
    void historyIsBoundedAndAckIsValidated(){
#ifndef LATENCY_BASELINE
        RealtimeSync sync;for(int n=0;n<10000;++n)sync.record({{"buttons",1}});QCOMPARE(sync.pending.size(),32);sync.sent();QVERIFY(!sync.validAck(10001));QVERIFY(!sync.validAck(-1));QVERIFY(!sync.validAck(0.5));
        sync.snapshot(200,9990,50);QCOMPARE(sync.pending.size(),10);QVERIFY(!sync.validAck(9989));sync.snapshot(240,10000,100);QVERIFY(sync.pending.isEmpty());QVERIFY(sync.interpolationMs()>=25&&sync.interpolationMs()<=80);QCOMPARE(sync.rttMs(),140);
#endif
    }
    // 验证“lost跳跃操作IsRetriedOnce”场景中的关键结果，断言失败时由测试框架报告。
    void lostJumpActionIsRetriedOnce(){
#ifndef LATENCY_BASELINE
        LinkService a("host"),b("guest");a.other=&b;b.other=&a;a.delay=b.delay=30;BadmintonController host(&a),guest(&b);QVERIFY(host.create("guest"));Message m;m.sender="host";m.text=a.invitation;QVERIFY(guest.openInvitation(m));QTRY_VERIFY_WITH_TIMEOUT(host.active()&&guest.active(),3000);
        b.dropNextInput=true;guest.setButtons(4);guest.setButtons(0);QTRY_VERIFY_WITH_TIMEOUT(host.state()["players"].toArray()[1].toArray()[1].toDouble()<520,800);QCOMPARE(b.dropped,1);
        QTest::qWait(800);QCOMPARE(host.state()["players"].toArray()[1].toArray()[1].toDouble(),522.0);QCOMPARE(host.state()["hits"].toInt(),0);
#endif
    }
    // 更新测试中的响应数据状态。
    void response_data(){QTest::addColumn<QString>("game");QTest::addColumn<int>("delay");QTest::addColumn<int>("jitter");QTest::addColumn<int>("loss");for(auto game:{QString("tank"),QString("badminton")}){QTest::newRow(qPrintable(game+"-120ms"))<<game<<60<<0<<0;QTest::newRow(qPrintable(game+"-jitter-loss"))<<game<<60<<15<<33;}}
    // 验证“响应”场景中的关键结果，断言失败时由测试框架报告。
    void response(){QFETCH(QString,game);QFETCH(int,delay);QFETCH(int,jitter);QFETCH(int,loss);
        LinkService a("host"),b("guest");a.other=&b;b.other=&a;a.delay=b.delay=delay;a.jitter=b.jitter=jitter;a.lossEvery=b.lossEvery=loss;
        // 验证“exercise”场景中的关键结果，断言失败时由测试框架报告。
        auto exercise=[&](auto& host,auto& guest){
            QVERIFY(host.create("guest"));Message m;m.sender="host";m.text=a.invitation;QVERIFY(guest.openInvitation(m));QTRY_VERIFY_WITH_TIMEOUT(guest.active()&&host.active(),3500);
            QTest::qWait(180);/* 返回测试使用的位置。 */ auto position=[&](bool predicted){auto s=predicted?view(guest):guest.state();return game=="tank"?s["tanks"].toArray()[1].toArray()[0].toDouble():s["players"].toArray()[1].toArray()[0].toDouble();};
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
    // 在套件完成后释放共用的测试资源。
    void cleanupTestCase(){const auto file=qEnvironmentVariable("REALTIME_RESULTS");if(!file.isEmpty()){QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(records).toJson());}}
};
// 登记 RealtimeTest 的 Qt 测试入口，由测试框架初始化环境并执行断言。
QTEST_MAIN(RealtimeTest)
#include "tst_realtime.moc"
