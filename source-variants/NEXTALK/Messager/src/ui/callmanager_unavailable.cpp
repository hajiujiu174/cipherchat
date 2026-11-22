#include "callmanager.h"
#include "services/chatservice.h"
#include <QWidget>
namespace Messager {
CallManager::CallManager(ChatService* service,QWidget* owner):QObject(owner),m_service(service),m_owner(owner){setObjectName("callManager");}
CallManager::~CallManager()=default;
void CallManager::invite(const QString&,bool){emit m_service->operationError("此构建未包含语音视频组件");}
}
