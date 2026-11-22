#include "appcontroller.h"
namespace Messager {
// 构造桌面应用控制器，初始化后续操作需要的状态或依赖。
AppController::AppController(ChatService* service, QObject* parent) : QObject(parent), m_service(service) {
    // 选择appcontroller 模块。
    auto choose = [this] {
        if (!m_service->conversation(m_selected).id.isEmpty())
            return;
        m_selected = m_service->conversations().isEmpty() ? QString() : m_service->conversations().first().id;
        m_service->markViewed(m_selected);
        emit selectionChanged(m_selected);
    };
    connect(service, &ChatService::conversationsChanged, this, choose);
    choose();
}
// 切换到有效的会话，更新查看状态并通知界面刷新选中项。
void AppController::selectConversation(const QString& id) {
    if (m_service->conversation(id).id.isEmpty())
        return;
    m_selected = id;
    m_service->markViewed(id);
    emit selectionChanged(id);
}
// 打开私聊会话。
void AppController::openDirectConversation(const QString& username) {
    const auto id = m_service->openDirectConversation(username);
    if (!id.isEmpty())
        selectConversation(id);
}
// 把正文和发送选项交给当前选中会话的聊天服务。
QString AppController::sendText(const QString& text, bool fail) {
    return m_service->send(m_selected, text, fail);
}
// 模拟收到的。
void AppController::simulateIncoming() {
    m_service->simulateIncoming(m_selected);
    m_service->markViewed(m_selected);
}
// 把已选择的附件路径交给当前会话的文件发送接口。
void AppController::attachFile(const QString& path) {
    m_service->sendFile(path, m_selected);
}
} // namespace Messager
