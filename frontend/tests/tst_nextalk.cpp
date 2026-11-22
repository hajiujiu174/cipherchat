#include "services/nextalkgateway.h"
#include "services/nextalkmapping.h"
#include <QtTest>
using namespace Messager;
class NextalkAdapterTest : public QObject {
    Q_OBJECT
  private slots:
    // 验证“认证Optional账号范围Contract”场景中的关键结果，断言失败时由测试框架报告。
    void authenticationOptionalScopeContract() {
        NextalkGateway gateway;
        QJsonObject request{{"command", "authenticate"},
                            {"host", "127.0.0.1"},
                            {"port", 55220},
                            {"caFile", "test-cert.pem"},
                            {"serverName", "localhost"},
                            {"dataDirectory", "test-profile"},
                            {"username", "fixture"},
                            {"password", "fixture-password"},
                            {"registerNew", false}};
        QVERIFY(gateway.validateCommand(request));
        request["accountScope"] = QJsonValue::Null;
        QVERIFY(!gateway.validateCommand(request));
        request["accountScope"] = "";
        QVERIFY(gateway.validateCommand(request));
    }
    // 验证“real运行环境命令Contract”场景中的关键结果，断言失败时由测试框架报告。
    void realRuntimeCommandContract() {
        NextalkGateway gateway;
        QVERIFY(gateway.validateCommand(NextalkMapping::sendText("bob", "hello")));
        QVERIFY(gateway.validateCommand(NextalkMapping::sendGroup(42, "group text")));
        QVERIFY(gateway.validateCommand(NextalkMapping::sendFile("bob", "D:/demo.txt")));
        QVERIFY(gateway.validateCommand({{"command", "resumeFile"}, {"fileId", "file-1"}}));
        QVERIFY(gateway.validateCommand({{"command", "cancelFile"}, {"fileId", "file-1"}}));
        QVERIFY(gateway.validateCommand({{"command", "pauseFile"}, {"fileId", "file-1"}}));
        QVERIFY(!gateway.validateCommand({{"command", "pauseFile"}}));
        QVERIFY(!gateway.validateCommand({{"command", "sendText"}}));
    }
    // 验证“disconnected队列AndCorrelation”场景中的关键结果，断言失败时由测试框架报告。
    void disconnectedQueueAndCorrelation() {
        NextalkGateway gateway;
        QSignalSpy events(&gateway, &NextalkGateway::eventReceived);
        QVERIFY(gateway.post(NextalkMapping::sendText("bob", "local contract test"), "request-17"));
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 1200);
        bool correlated = false;
        for (const auto& event : events) {
            auto o = event[0].toJsonObject();
            if (o["type"] == "commandResult") {
                QCOMPARE(o["requestId"].toString(), QString("request-17"));
                QVERIFY(!o["accepted"].toBool());
                correlated = true;
            }
        }
        QVERIFY(correlated);
    }
    // 验证“truthful投递And进度映射”场景中的关键结果，断言失败时由测试框架报告。
    void truthfulDeliveryAndProgressMapping() {
        QCOMPARE(NextalkMapping::deliveryLabel(1), QString("服务器已接收"));
        QCOMPARE(NextalkMapping::deliveryLabel(2), QString("对端已签收"));
        QVERIFY(!NextalkMapping::deliveryLabel(2).contains("已读"));
        auto m = NextalkMapping::message({{"messageId", "42"},
                                             {"from", "alice"},
                                             {"to", "bob"},
                                             {"text", "test"},
                                             {"groupId", 0},
                                             {"timestamp", 0},
                                             {"state", 0}},
                                            "alice");
        QCOMPARE(m.conversation, QString("peer:bob"));
        QVERIFY(m.outgoing);
        QCOMPARE(m.time, QString("时间未记录"));
        QCOMPARE(m.transportStatus, QString("本地待发送"));
        auto t = NextalkMapping::file({{"fileId", "f1"},
                                          {"peer", "bob"},
                                          {"name", "x"},
                                          {"authenticatedBytes", 25},
                                          {"totalBytes", 100},
                                          {"complete", false}});
        QCOMPARE(t.progress, 25);
        QCOMPARE(t.state, TransferState::Running);
        t = NextalkMapping::file({{"fileId", "empty"}, {"totalBytes", 0}, {"complete", true}});
        QCOMPARE(t.progress, 100);
    }
};
// 登记 NextalkAdapterTest 的 Qt 测试入口，由测试框架初始化环境并执行断言。
QTEST_MAIN(NextalkAdapterTest)
#include "tst_nextalk.moc"
