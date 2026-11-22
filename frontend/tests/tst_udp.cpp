#include "features/games/gameudp.h"
#include <QtTest>
#include <QProcess>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QMessageAuthenticationCode>
#include <QDateTime>
#include <QUuid>
using namespace Messager;
class UdpTest:public QObject {
    Q_OBJECT
    QProcess relay;quint16 port=0;
    const QByteArray secret="synthetic-loopback-udp-test-secret-20261114";
    // 返回测试使用的票据。
    QJsonObject ticket(const QString& user){const auto expires=QDateTime::currentSecsSinceEpoch()+300;const auto session=QUuid::createUuid().toString(QUuid::WithoutBraces);const auto text=user.toUtf8()+"\n"+QByteArray::number(expires)+"\n"+session.toUtf8();return {{"expires",expires},{"session",session},{"key",QString::fromLatin1(QMessageAuthenticationCode::hash(text,secret,QCryptographicHash::Sha256).toBase64())}};}
    // 验证“launch”场景中的关键结果，断言失败时由测试框架报告。
    void launch(quint16 requested=0){
        relay.start("python",{GAME_UDP_SCRIPT,"--bind","127.0.0.1","--port",QString::number(requested)});
        QVERIFY(relay.waitForStarted(5000));QVERIFY(relay.waitForReadyRead(5000));
        port=quint16(QJsonDocument::fromJson(relay.readLine()).object()["port"].toInt());QVERIFY(port);
    }
    // 关闭测试。
    void close(){relay.terminate();if(!relay.waitForFinished(3000)){relay.kill();relay.waitForFinished();}}
private slots:
    // 初始化测试。
    void init(){qputenv("NEXTALK_GAME_UDP_SECRET",secret);launch();}
    // 清理测试。
    void cleanup(){close();}
    // 验证“中转And备用路径”场景中的关键结果，断言失败时由测试框架报告。
    void relayAndFallback(){
        GameUdp a,b;a.start("localhost",port,"a",ticket("a"));b.start("127.0.0.1",port,"b",ticket("b"));
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
    // 验证“boundedPacketsAnd会话重置”场景中的关键结果，断言失败时由测试框架报告。
    void boundedPacketsAndSessionReset(){
        GameUdp a,b;a.start("127.0.0.1",port,"a",ticket("a"));b.start("127.0.0.1",port,"b",ticket("b"));
        a.advertise("b",true);b.advertise("a",true);QTRY_VERIFY(a.ready("b"));
        QSignalSpy received(&b,&GameUdp::packet);
        QJsonObject invalid{{"game","chess"},{"type","state"},{"net",2}};
        QVERIFY(a.send("b",invalid));QTest::qWait(100);QCOMPARE(received.size(),0);
        invalid["game"]="badminton";invalid["filler"]=QString(33000,'x');QVERIFY(!a.send("b",invalid));
        a.stop();QVERIFY(!a.capable());QVERIFY(!a.ready("b"));QVERIFY(!a.send("b",{}));
        a.start("invalid-host",port,"a",ticket("a"));QVERIFY(!a.capable());
    }
    // 验证“departed端点DoesNotStop中转”场景中的关键结果，断言失败时由测试框架报告。
    void departedEndpointDoesNotStopRelay(){
        QUdpSocket departed;QVERIFY(departed.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
        const QByteArray registration="{\"v\":1,\"op\":\"bind\",\"from\":\"departed\"}";
        departed.writeDatagram(registration,QHostAddress::LocalHost,port);
        QTest::qWait(100);QVERIFY(!departed.hasPendingDatagrams());departed.close();
        QUdpSocket sender;
        const QByteArray stale="{\"v\":1,\"op\":\"pong\",\"from\":\"departed\",\"to\":\"departed\"}";
        for(int n=0;n<30;++n){sender.writeDatagram(stale,QHostAddress::LocalHost,port);QTest::qWait(5);}
        GameUdp a,b;a.start("127.0.0.1",port,"next-a",ticket("next-a"));b.start("127.0.0.1",port,"next-b",ticket("next-b"));
        a.advertise("next-b",true);b.advertise("next-a",true);
        QTRY_VERIFY_WITH_TIMEOUT(a.ready("next-b")&&b.ready("next-a"),3000);
    }
};
// 登记 UdpTest 的 Qt 测试入口，由测试框架初始化环境并执行断言。
QTEST_GUILESS_MAIN(UdpTest)
#include "tst_udp.moc"
