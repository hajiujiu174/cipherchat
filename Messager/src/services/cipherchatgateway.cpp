#include "cipherchatgateway.h"
#include "business_worker.hpp"
#include "rhine_json.hpp"
#include <QJsonDocument>
#include <QTimer>
#include <QElapsedTimer>
namespace Messager {
struct CipherChatGateway::Impl {
    ccfw::gui::BusinessWorker worker;
    QTimer timer;
    QElapsedTimer clock;
    qint64 fastUntil=0;
};
CipherChatGateway::CipherChatGateway(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->timer.setInterval(30);
    m_impl->timer.setTimerType(Qt::PreciseTimer);m_impl->clock.start();
    connect(&m_impl->timer, &QTimer::timeout, this, [this] {
        for (const auto& event : m_impl->worker.drain()) {
            // The shared serializer handles ClientEvent and RestoredView. CommandResult
            // needs its own branch to preserve correlation and avoid a variant mismatch.
            QJsonObject result;
            if (const auto* r = std::get_if<ccfw::gui::CommandResult>(&event)) {
                result = {{"type", "commandResult"},
                          {"requestId", QString::fromStdString(r->requestId)},
                          {"accepted", r->accepted},
                          {"operationId", QString::fromStdString(r->operationId)}};
                if (r->error)
                    result["error"] = QJsonObject{{"code", int(r->error->code)},
                                                  {"message", QString::fromStdString(r->error->message)}};
            } else {
                const auto data = ccfw::gui::rhine::eventJson(event).dump();
                result = QJsonDocument::fromJson(QByteArray::fromStdString(data)).object();
            }
            emit eventReceived(result);
        }
        const int interval=m_impl->clock.elapsed()<m_impl->fastUntil?5:30;
        if(m_impl->timer.interval()!=interval)m_impl->timer.setInterval(interval);
    });
    m_impl->timer.start();
}
CipherChatGateway::~CipherChatGateway() {
    m_impl->timer.stop();
    m_impl->worker.requestStop();
    m_impl->worker.join();
}
bool CipherChatGateway::validateCommand(const QJsonObject& request) const {
    try {
        const auto data = QJsonDocument(request).toJson(QJsonDocument::Compact);
        ccfw::gui::rhine::commandFromJson(nlohmann::json::parse(data.constData()));
        return true;
    } catch (...) {
        return false;
    }
}
bool CipherChatGateway::post(const QJsonObject& request, const QString& requestId) {
    try {
        if(request["command"]=="gameRelay") {
            m_impl->fastUntil=m_impl->clock.elapsed()+3000;
            if(m_impl->timer.interval()!=5)m_impl->timer.setInterval(5);
        }
        const auto data = QJsonDocument(request).toJson(QJsonDocument::Compact);
        auto command = ccfw::gui::rhine::commandFromJson(nlohmann::json::parse(data.constData()));
        if (auto* text = std::get_if<ccfw::SendText>(&command))
            text->messageId = request.value("messageId").toString().toStdString();
        if (m_impl->worker.post(std::move(command), requestId.toStdString()))
            return true;
        emit commandRejected("业务命令队列已满、已关闭或该命令不支持关联标识");
    } catch (...) {
        emit commandRejected(request.value("command").toString() == "authenticate"
                                 ? "登录信息或连接配置格式有误，请检查连接设置"
                                 : "操作信息格式有误，未能提交给 NEXTALK");
    }
    return false;
}
} // namespace Messager
