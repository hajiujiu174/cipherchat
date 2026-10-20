#include "services/cipherchatgateway.h"
#include "services/cipherchatmapping.h"
#include <QtTest>
using namespace Messager;
class CipherChatAdapterTest : public QObject {
    Q_OBJECT
  private slots:
    void authenticationOptionalScopeContract() {
        CipherChatGateway gateway;
        QJsonObject request{{"command", "authenticate"},
                            {"host", "127.0.0.1"},
                            {"port", 55220},
                            {"caFile", "test-cert.pem"},
                            {"serverName", "localhost"},
                            {"dataDirectory", "test-profile"},
                            {"username", "fixture"},
                            {"password", "fixture-password"},
                            {"registerNew", false},
                            {"directEnabled", false}};
        QVERIFY(gateway.validateCommand(request));
        request["accountScope"] = QJsonValue::Null;
        QVERIFY(!gateway.validateCommand(request));
        request["accountScope"] = "";
        QVERIFY(gateway.validateCommand(request));
    }
    void realRuntimeCommandContract() {
        CipherChatGateway gateway;
        QVERIFY(gateway.validateCommand(CipherChatMapping::sendText("bob", "hello")));
        QVERIFY(gateway.validateCommand(CipherChatMapping::sendGroup(42, "group text")));
        QVERIFY(gateway.validateCommand(CipherChatMapping::sendFile("bob", "D:/demo.txt")));
        QVERIFY(gateway.validateCommand({{"command", "resumeFile"}, {"fileId", "file-1"}}));
        QVERIFY(gateway.validateCommand({{"command", "cancelFile"}, {"fileId", "file-1"}}));
        QVERIFY(!gateway.validateCommand({{"command", "pauseFile"}, {"fileId", "file-1"}}));
        QVERIFY(!gateway.validateCommand({{"command", "sendText"}}));
    }
    void disconnectedQueueAndCorrelation() {
        CipherChatGateway gateway;
        QSignalSpy events(&gateway, &CipherChatGateway::eventReceived);
        QVERIFY(gateway.post(CipherChatMapping::sendText("bob", "local contract test"), "request-17"));
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
    void truthfulDeliveryAndProgressMapping() {
        QCOMPARE(CipherChatMapping::deliveryLabel(1), QString("服务器已接收"));
        QCOMPARE(CipherChatMapping::deliveryLabel(2), QString("对端已签收"));
        QVERIFY(!CipherChatMapping::deliveryLabel(2).contains("已读"));
        auto m = CipherChatMapping::message({{"messageId", "42"},
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
        auto t = CipherChatMapping::file({{"fileId", "f1"},
                                          {"peer", "bob"},
                                          {"name", "x"},
                                          {"authenticatedBytes", 25},
                                          {"totalBytes", 100},
                                          {"complete", false}});
        QCOMPARE(t.progress, 25);
        QCOMPARE(t.state, TransferState::Running);
        t = CipherChatMapping::file({{"fileId", "empty"}, {"totalBytes", 0}, {"complete", true}});
        QCOMPARE(t.progress, 100);
    }
};
QTEST_MAIN(CipherChatAdapterTest)
#include "tst_cipherchat.moc"
