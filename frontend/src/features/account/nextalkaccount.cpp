#include "services/nextalkservice.h"
#include "services/nextalkmapping.h"
#include <QTimer>
#include <QFileInfo>

namespace Messager {

// 请求注册验证码。
void NextalkService::requestRegistrationCode(const QJsonObject& request) {
    auto command = request;
    command["requestCodeOnly"] = true; command["registerNew"] = false; command["password"] = "";
    authenticate(command);
}

// 重置密码。
void NextalkService::resetPassword(const QJsonObject& request) {
    auto command=request; command["passwordReset"]=true; command["registerNew"]=false; command["requestCodeOnly"]=false;
    authenticate(command);
}

// 检查登录请求和证书，准备重连所需状态并向业务线程提交认证命令。
void NextalkService::authenticate(const QJsonObject& request) {
    if (request["username"].toString().trimmed().isEmpty() || (!request["requestCodeOnly"].toBool() && !request["emailLogin"].toBool() && request["password"].toString().isEmpty())) {
        error("请输入账号和密码");
        return;
    }
    if (!QFileInfo::exists(request["caFile"].toString())) {
        error("未找到服务器证书，请检查连接设置");
        return;
    }
    QJsonObject command = request;
    command["command"] = "authenticate";
    saveViewState();++m_reconnectGeneration;m_hadOnline=false;m_reconnectAttempt=0;m_reconnectRequest={};
    if(!request["passwordReset"].toBool()&&!request["requestCodeOnly"].toBool()&&!request["emailLogin"].toBool()){
        m_reconnectRequest=command;m_reconnectRequest["registerNew"]=false;m_reconnectRequest.remove("emailCode");m_reconnectRequest.remove("email");
    }
    m_config = request;
    m_config.remove("password");
    m_config.remove("emailCode");
    m_config.remove("email");
    m_config.remove("requestCodeOnly");
    m_config.remove("passwordReset");
    m_config.remove("emailLogin");
    m_config.remove("registerNew");
    m_config.remove("command");
    m_user = request["username"].toString().trimmed();
    m_online = false;
    resetView();
    m_status = "正在验证服务器连接…";
    emit sessionChanged();
    if (!m_gateway.post(command)) {
        m_status = "连接请求未提交";
        emit sessionChanged();
    }
}

// 主动断开当前账号、作废自动重连并刷新桌面状态。
void NextalkService::disconnectAccount() {
    saveViewState();++m_reconnectGeneration;m_hadOnline=false;m_reconnectRequest={};
    m_gateway.post({{"command", "disconnect"}});
    m_online = false;
    resetView();
    m_status = "已退出登录";
    emit sessionChanged();
}

} // namespace Messager
