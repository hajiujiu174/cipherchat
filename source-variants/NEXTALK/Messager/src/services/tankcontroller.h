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
    explicit TankController(ChatService*,QObject* parent=nullptr);
    bool create(const QString& peer);
    bool openInvitation(const Message&);
    void leave();
    void setButtons(int buttons);
    void setPointerTarget(const QPointF& target);
    void setPointerFire(bool down);
    void stopInput();
    bool pointerActive() const {return m_pointerActive;}
    QPointF pointerTarget() const {return m_target;}
    QJsonObject state() const {return m_state;}
    QJsonObject predictedState() const {return !m_host&&m_peerNet2&&!m_renderState.isEmpty()?m_renderState:m_state;}
    QJsonObject networkStats() const {return m_sync.stats();}
    int interpolationMs() const {return m_peerNet2?m_sync.interpolationMs():100;}
    QString networkText() const;
    QString status() const {return m_status;}
    QString room() const {return m_room;}
    QString peer() const {return m_peer;}
    bool hosting() const {return m_host;}
    bool active() const {return m_joined;}
signals:
    void changed();
    void chatRequested(const QString&);
private:
    void receive(const QString&,const QJsonObject&);
    void pump();
    void rebuildPrediction();
    void packet(QJsonObject);
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
