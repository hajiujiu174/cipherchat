#include "services/gameudp.h"
#include <QtTest>
#include <QProcess>
#include <QJsonDocument>
#include <QSignalSpy>
using namespace Messager;
class UdpTest:public QObject {
    Q_OBJECT
    QProcess relay;quint16 port=0;
    void launch(quint16 requested=0){
        relay.start("python",{GAME_UDP_SCRIPT,"--bind","127.0.0.1","--port",QString::number(requested)});
        QVERIFY(relay.waitForStarted(5000));QVERIFY(relay.waitForReadyRead(5000));
        port=quint16(QJsonDocument::fromJson(relay.readLine()).object()["port"].toInt());QVERIFY(port);
    }
    void close(){relay.terminate();if(!relay.waitForFinished(3000)){relay.kill();relay.waitForFinished();}}
private slots:
    void init(){launch();}
    void cleanup(){close();}
    void relayAndFallback(){
        GameUdp a,b;a.start("127.0.0.1",port,"a");b.start("127.0.0.1",port,"b");
        a.advertise("b",true);b.advertise("a",true);
        QTRY_VERIFY_WITH_TIMEOUT(a.ready("b")&&b.ready("a"),3000);
        QSignalSpy received(&b,&GameUdp::packet);
        QJsonObject frame{{"game","tanktrouble"},{"type","input"},{"net",2},{"seq",1},{"buttons",2}};
        QVERIFY(a.send("b",frame));QTRY_COMPARE(received.size(),1);
        QCOMPARE(received[0][0].toString(),QString("a"));QCOMPARE(received[0][1].toJsonObject(),frame);
        close();QTest::qWait(1100);QVERIFY(!a.ready("b"));QVERIFY(!a.send("b",frame));
        launch(port);QTRY_VERIFY_WITH_TIMEOUT(a.ready("b")&&b.ready("a"),3000);
        frame["game"]="badminton";frame["seq"]=2;
        QVERIFY(a.send("b",frame));QTRY_COMPARE(received.size(),2);
        QVERIFY(a.stats()["tls_fallback"].toInt()>0);QCOMPARE(b.stats()["received"].toInt(),2);
    }
    void boundedPacketsAndSessionReset(){
        GameUdp a,b;a.start("127.0.0.1",port,"a");b.start("127.0.0.1",port,"b");
        a.advertise("b",true);b.advertise("a",true);QTRY_VERIFY(a.ready("b"));
        QSignalSpy received(&b,&GameUdp::packet);
        QJsonObject invalid{{"game","chess"},{"type","state"},{"net",2}};
        QVERIFY(a.send("b",invalid));QTest::qWait(100);QCOMPARE(received.size(),0);
        invalid["game"]="badminton";invalid["filler"]=QString(33000,'x');QVERIFY(!a.send("b",invalid));
        a.stop();QVERIFY(!a.capable());QVERIFY(!a.ready("b"));QVERIFY(!a.send("b",{}));
        a.start("invalid-host",port,"a");QVERIFY(!a.capable());
    }
    void departedEndpointDoesNotStopRelay(){
        QUdpSocket departed;QVERIFY(departed.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
        const QByteArray registration="{\"v\":1,\"op\":\"bind\",\"from\":\"departed\"}";
        departed.writeDatagram(registration,QHostAddress::LocalHost,port);
        QTRY_VERIFY(departed.hasPendingDatagrams());departed.close();
        QUdpSocket sender;
        const QByteArray stale="{\"v\":1,\"op\":\"pong\",\"from\":\"departed\",\"to\":\"departed\"}";
        for(int n=0;n<30;++n){sender.writeDatagram(stale,QHostAddress::LocalHost,port);QTest::qWait(5);}
        GameUdp a,b;a.start("127.0.0.1",port,"next-a");b.start("127.0.0.1",port,"next-b");
        a.advertise("next-b",true);b.advertise("next-a",true);
        QTRY_VERIFY_WITH_TIMEOUT(a.ready("next-b")&&b.ready("next-a"),3000);
    }
};
QTEST_GUILESS_MAIN(UdpTest)
#include "tst_udp.moc"
