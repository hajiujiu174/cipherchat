#pragma once
#include "tankengine.h"
#include "core/types.h"
#include "realtimesync.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
namespace Messager {
class ChatService;
class TankController final:public QObject {
    Q_OBJECT
public:
    // 构造坦克联机控制器，初始化后续操作需要的状态或依赖。
    explicit TankController(ChatService*,QObject* parent=nullptr);
    // 创建坦克联机控制器。
    bool create(const QString& peer);
    // 打开邀请。
    bool openInvitation(const Message&);
    // 离开坦克联机控制器。
    void leave();
    // 设置坦克联机控制器使用的按键状态。
    void setButtons(int buttons);
    // 设置坦克联机控制器使用的指针目标。
    void setPointerTarget(const QPointF& target);
    // 设置坦克联机控制器使用的指针Fire。
    void setPointerFire(bool down);
    // 停止输入。
    void stopInput();
    // 返回坦克联机控制器使用的指针活动状态。
    bool pointerActive() const {return m_pointerActive;}
    // 返回坦克联机控制器使用的指针目标。
    QPointF pointerTarget() const {return m_target;}
    // 返回坦克联机控制器使用的状态。
    QJsonObject state() const {return m_state;}
    // 返回坦克联机控制器使用的预测的状态。
    QJsonObject predictedState() const {return !m_host&&m_peerNet2&&!m_renderState.isEmpty()?m_renderState:m_state;}
    // 返回坦克联机控制器使用的联机传输统计。
    QJsonObject networkStats() const {return m_sync.stats();}
    // 返回坦克联机控制器使用的插值毫秒。
    int interpolationMs() const {return m_peerNet2?m_sync.interpolationMs():100;}
    // 返回坦克联机控制器使用的网络文本。
    QString networkText() const;
    // 返回坦克联机控制器使用的状态。
    QString status() const {return m_status;}
    // 返回坦克联机控制器使用的房间。
    QString room() const {return m_room;}
    // 返回坦克联机控制器使用的对端。
    QString peer() const {return m_peer;}
    // 返回坦克联机控制器使用的房主状态。
    bool hosting() const {return m_host;}
    // 判断坦克联机控制器的是否处于活动状态。
    bool active() const {return m_joined;}
signals:
    // 通知订阅方组件状态已经变化。
    void changed();
    // 通知调用方已请求聊天操作。
    void chatRequested(const QString&);
private:
    // 接收坦克联机控制器。
    void receive(const QString&,const QJsonObject&);
    // 推进坦克联机控制器。
    void pump();
    // 重建预测。
    void rebuildPrediction();
    // 返回坦克联机控制器使用的数据包。
    void packet(QJsonObject);
    // 清除坦克联机控制器。
    void clear(const QString&);
    ChatService* m_service;
    TankEngine m_engine,m_prediction;
    RealtimeSync m_sync;
    QJsonObject m_renderState,m_remoteInput;
    bool m_peerNet2=false,m_paused=false;
    int m_remoteTick=0,m_shotSequence=0,m_remoteShot=0,m_jumpSequence=0,m_remoteJump=0,m_swingSequence=0,m_remoteSwing=0;
    qint64 m_echo=-1;
    int m_remoteArenaAck=0;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QString m_room,m_peer,m_owner,m_run,m_status="选择在线好友，创建双人房间。";
    QJsonObject m_state;
    bool m_host=false,m_joined=false;
    bool m_pointerActive=false,m_pointerFire=false,m_fireQueued=false;
    QPointF m_target;
    int m_buttons=0,m_sequence=0,m_remoteSequence=-1,m_lastFrame=-1;
    qint64 m_heard=0,m_sent=0,m_inputAt=0;
};
}
