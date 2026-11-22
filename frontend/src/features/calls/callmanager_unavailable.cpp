#include "callmanager.h"
#include "services/chatservice.h"
#include <QWidget>
namespace Messager {
// 构造音视频通话控制器，初始化后续操作需要的状态或依赖。
CallManager::CallManager(ChatService* service,QWidget* owner):QObject(owner),m_service(service),m_owner(owner){setObjectName("callManager");}
// 销毁音视频通话控制器时释放它持有的资源。
CallManager::~CallManager()=default;
// 邀请音视频通话控制器。
void CallManager::invite(const QString&,bool){emit m_service->operationError("此构建未包含语音视频组件");}
}
