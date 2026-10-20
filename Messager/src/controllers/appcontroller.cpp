#include "appcontroller.h"
namespace Messager {
AppController::AppController(ChatService* service, QObject* parent) : QObject(parent), m_service(service) {
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
void AppController::selectConversation(const QString& id) {
    if (m_service->conversation(id).id.isEmpty())
        return;
    m_selected = id;
    m_service->markViewed(id);
    emit selectionChanged(id);
}
void AppController::openDirectConversation(const QString& username) {
    const auto id = m_service->openDirectConversation(username);
    if (!id.isEmpty())
        selectConversation(id);
}
QString AppController::sendText(const QString& text, bool fail) {
    return m_service->send(m_selected, text, fail);
}
void AppController::simulateIncoming() {
    m_service->simulateIncoming(m_selected);
    m_service->markViewed(m_selected);
}
void AppController::attachFile(const QString& path) {
    m_service->sendFile(path, m_selected);
}
} // namespace Messager
