#include "tankcontroller.h"
#include "chatservice.h"
#include "gomokuwire.h"
#include <QUuid>
#include <cmath>
namespace Messager {
TankController::TankController(ChatService* service,QObject* parent):QObject(parent),m_service(service) {
    m_clock.start();m_state=m_engine.snapshot();m_timer.setInterval(20);m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer,&QTimer::timeout,this,&TankController::pump);m_timer.start();
    connect(service,&ChatService::gamePacket,this,&TankController::receive);
    connect(service,&ChatService::sessionChanged,this,[this] {
        if(!m_room.isEmpty() && (!m_service->online()||m_owner!=m_service->username()))clear("连接已断开，请重新邀请好友开局。");
        emit changed();
    });
    connect(service,&ChatService::gameRelayChanged,this,&TankController::changed);
}
void TankController::clear(const QString& text) {
    m_sync.reset();m_renderState={};m_remoteInput={};m_peerNet2=false;m_paused=false;m_remoteTick=m_shotSequence=m_remoteShot=m_jumpSequence=m_remoteJump=m_swingSequence=m_remoteSwing=0;m_echo=-1;m_room.clear();m_peer.clear();m_run.clear();m_joined=false;stopInput();m_status=text;emit changed();
}
void TankController::packet(QJsonObject data) {
    if(m_room.isEmpty())return;
    data["net"]=(data["type"]=="input"&&!m_peerNet2)?1:2;data["room"]=m_room;data["game"]="tanktrouble";data["v"]=1;data["run"]=m_run;
    m_service->sendGamePacket(m_peer,data);
}
bool TankController::create(const QString& peer) {
    if(!m_room.isEmpty()) {m_status="请先退出当前房间。";emit changed();return false;}
    if(!m_service->gameRelaySupported()||!m_service->isFriend(peer)) {
        m_status="请登录已启用实时游戏的服务器，并选择在线好友。";emit changed();return false;
    }
    bool online=false;for(const auto& c:m_service->contacts())if(c.username==peer)online=c.online;
    if(!online) {m_status="好友当前不在线。";emit changed();return false;}
    m_owner=m_service->username();m_peer=peer;m_room=QUuid::createUuid().toString(QUuid::Id128);
    m_run=QUuid::createUuid().toString(QUuid::Id128);m_host=true;m_joined=false;m_lastFrame=-1;m_sequence=0;m_remoteSequence=-1;
    m_sync.reset();m_renderState={};m_remoteInput={};m_peerNet2=false;m_paused=false;m_remoteTick=m_shotSequence=m_remoteShot=m_jumpSequence=m_remoteJump=m_swingSequence=m_remoteSwing=0;m_echo=-1;
    stopInput();m_engine.reset();m_state=m_engine.snapshot();m_heard=m_clock.elapsed();
    const auto conversation=m_service->openDirectConversation(peer);
    if(conversation.isEmpty() || m_service->send(conversation,GomokuWire::encode({{"game","tanktrouble"},{"type","invite"},{"room",m_room},{"black",m_owner},{"white",peer},{"run",m_run}})).isEmpty()) {
        clear("邀请发送失败，请稍后重试。");return false;
    }
    m_status="邀请已发送，等待好友加入。你是紫色坦克。";emit changed();emit chatRequested(peer);return true;
}
bool TankController::openInvitation(const Message& message) {
    const auto o=GomokuWire::decode(message.text);
    if(o.isEmpty()||o["game"]!="tanktrouble"||o["type"]!="invite"||message.outgoing||
       o["black"].toString()!=message.sender||o["white"].toString()!=m_service->username()||!m_service->isFriend(message.sender))return false;
    static const QRegularExpression id("^[0-9a-f]{32}$");
    if(!id.match(o["run"].toString()).hasMatch())return false;
    if(!m_room.isEmpty()) {m_status="请先退出当前房间再加入邀请。";emit changed();return false;}
    if(!m_service->gameRelaySupported()) {m_status="当前服务器尚未启用实时游戏，需要升级服务器后联机。";emit changed();return false;}
    m_owner=m_service->username();m_peer=message.sender;m_room=o["room"].toString();m_run=o["run"].toString();
    m_sync.reset();m_renderState={};m_peerNet2=false;m_paused=false;m_host=false;m_joined=false;m_sequence=0;m_remoteSequence=-1;m_lastFrame=-1;m_buttons=0;m_heard=m_clock.elapsed();m_sent=-1000;
    m_status="正在加入好友的房间……";emit changed();return true;
}
void TankController::leave() {
    const QJsonObject data{{"type","leave"},{"room",m_room},{"run",m_run},{"game","tanktrouble"},{"v",1}};
    const auto peer=m_peer;
    packet({{"type","leave"}});
    // Repeat control once after the server's per-connection rate window.
    if(!m_room.isEmpty())QTimer::singleShot(80,m_service,[service=m_service,peer,data]{service->sendGamePacket(peer,data);});
    clear("已退出房间，可以重新邀请好友。");
}
void TankController::setButtons(int buttons) {
    if((buttons&16)&&!(m_buttons&16))++m_shotSequence;
    m_buttons=buttons&31;
    if(m_buttons&15) {m_pointerActive=false;m_pointerFire=false;m_fireQueued=false;}
}
void TankController::setPointerTarget(const QPointF& target) {
    if(!m_joined||!std::isfinite(target.x())||!std::isfinite(target.y())||target.x()<0||target.x()>660||target.y()<0||target.y()>420)return;
    m_pointerActive=true;m_target=target;m_buttons&=16;
}
void TankController::setPointerFire(bool down) {
    if(!m_joined)return;
    if(down&&!m_pointerFire)++m_shotSequence;m_pointerFire=down;if(down)m_fireQueued=true;
}
void TankController::stopInput() {
    m_buttons=0;m_pointerActive=false;m_pointerFire=false;m_fireQueued=false;
}
void TankController::receive(const QString& peer,const QJsonObject& p) {
    if(m_room.isEmpty()||peer!=m_peer||p["room"]!=m_room||p["run"]!=m_run||p["v"].toInt()!=1||p["game"]!="tanktrouble")return;
    const auto type=p["type"].toString();
    if(type=="leave") {clear("好友已离开房间。");return;}
    if(m_host) {
        if(type=="join") {m_remoteArenaAck=0;m_peerNet2=p["net"].toInt()==2;m_joined=true;m_heard=m_clock.elapsed();m_status="对战中 · 你是紫色坦克";emit changed();}
        else if(type=="input"&&m_joined) {
            const auto seq=p["seq"].toInt(-1),b=p["buttons"].toInt(-1);
            if(seq<=m_remoteSequence||seq>1000000000||b<0||b>31)return;
            if(p.contains("mouse")&&!p["mouse"].isBool())return;
            if(p.contains("round")&&p["round"].toInt(-1)!=m_state["round"].toInt())return;
            if(p["mouse"].toBool()) {const auto t=p["target"].toArray();if(t.size()!=2||!t[0].isDouble()||!t[1].isDouble()||!std::isfinite(t[0].toDouble())||!std::isfinite(t[1].toDouble())||t[0].toDouble()<0||t[0].toDouble()>660||t[1].toDouble()<0||t[1].toDouble()>420)return;}
            if(p["net"].toInt()==2&&(!p["shot"].isDouble()||p["shot"].toDouble()!=p["shot"].toInt(-1)||p["shot"].toInt()<m_remoteShot||p["shot"].toInt()>1000000000))return;
            if(p["net"].toInt()==2) {
                if(!p["tick"].isDouble()||p["tick"].toDouble()!=p["tick"].toInt(-1)||p["tick"].toInt(-1)<m_remoteTick||p["tick"].toInt()>1000000000)return;
                m_remoteTick=p["tick"].toInt();m_echo=p["sent"].toInteger(-1);
                m_remoteArenaAck=p["arenaAck"].toInt(0);
            }
            if(p["net"].toInt()==2){if(p["shot"].toInt()>m_remoteShot)m_remoteInput["pulse"]=true;m_remoteShot=p["shot"].toInt();}
            const bool pulse=m_remoteInput["pulse"].toBool();m_remoteInput=p;m_remoteInput["pulse"]=pulse;
            m_remoteSequence=seq;m_heard=m_inputAt=m_clock.elapsed();
        }
    } else if(type=="state") {
        auto s=p["state"].toObject();
        if(!s.contains("walls")&&m_joined&&s["round"]==m_state["round"])s["walls"]=m_state.value("walls");
        if(!TankEngine::validSnapshot(s)||s["frame"].toInt()<m_lastFrame)return;
        if(m_joined&&m_state["round"]!=s["round"])stopInput();
        const auto previousView=predictedState();
        const bool modern=p["net"].toInt()==2;
        if(modern&&!m_sync.validAck(p["ackTick"]))return;
        if(m_state["round"]!=s["round"]||p["paused"].toBool())m_sync.clearPrediction();
        m_peerNet2=modern;m_paused=p["paused"].toBool();
        if(modern)m_sync.snapshot(m_clock.elapsed(),p["ackTick"].toInt(),p["echo"].toInteger(-1));
        m_lastFrame=s["frame"].toInt();m_state=s;m_heard=m_clock.elapsed();m_joined=true;
        rebuildPrediction();
        if(modern&&previousView["round"]==m_renderState["round"]){auto a=previousView["tanks"].toArray()[1].toArray(),b=m_renderState["tanks"].toArray()[1].toArray();const double distance=std::hypot(a[0].toDouble()-b[0].toDouble(),a[1].toDouble()-b[1].toDouble());if(distance>4)m_sync.corrected(distance);}
        m_status=p["paused"].toBool()?"连接暂时中断，双方已暂停。":m_state["finished"].toBool()?"本场结束 · 先得 5 分获胜，请退出后重新邀请。":"对战中 · 你是绿色坦克";emit changed();
    }
}
void TankController::pump() {
    if(m_room.isEmpty())return;
    const auto now=m_clock.elapsed();
    if(!m_service->online()||!m_service->gameRelaySupported()) {clear("连接已断开，请重新邀请好友开局。");return;}
    if(m_host && m_joined) {
        const bool paused=now-m_heard>1500;
        const bool fire=m_pointerFire||m_fireQueued||(m_buttons&16);
        if(m_pointerActive&&!paused)m_engine.pointerInput(0,m_target,fire);
        else m_engine.input(0,paused?0:m_buttons|(fire?16:0));
        m_fireQueued=false;
        if(now-m_inputAt>350||paused)m_engine.input(1,0);
        else {const auto& r=m_remoteInput;const int b=r["buttons"].toInt();const bool fire=(b&16)||r["pulse"].toBool();
            if(r["mouse"].toBool()){auto t=r["target"].toArray();if(t.size()==2)m_engine.pointerInput(1,QPointF(t[0].toDouble(),t[1].toDouble()),fire);}
            else m_engine.input(1,(b&15)|(fire?16:0));}
        m_remoteInput["pulse"]=false;
        if(!paused)m_engine.step();
        const auto next=m_engine.snapshot();
        if(next["round"]!=m_state["round"]) {stopInput();m_remoteInput={};m_sync.clearPrediction();}
        m_state=next;
        m_status=paused?"好友连接中断，对局已暂停，等待恢复。":m_state["finished"].toBool()?"本场结束 · 先得 5 分获胜，请退出后重新邀请。":"对战中 · 你是紫色坦克";
        if(now-m_sent>=40) {m_sent=now;auto wire=m_state;if(m_peerNet2&&m_remoteArenaAck==m_state["round"].toInt())wire.remove("walls");packet({{"type","state"},{"state",wire},{"paused",paused},{"ackTick",m_remoteTick},{"echo",m_echo}});}
        emit changed();
    } else if(!m_host) {
        if(m_joined&&m_peerNet2&&!m_paused&&now-m_heard<250){QJsonObject value{{"buttons",m_buttons&15},{"mouse",m_pointerActive}};if(m_pointerActive)value["target"]=QJsonArray{m_target.x(),m_target.y()};m_sync.record(value);rebuildPrediction();emit changed();}
        if(now-m_sent>=(m_joined?40:500)) {
            m_sent=now;
            if(!m_joined)packet({{"type","join"}});
            else {
                int buttons=m_buttons;
                // A keyboard equivalent keeps mouse guests usable with older hosts.
                if(m_pointerActive) {
                    buttons=0;const auto tank=m_state["tanks"].toArray()[1].toArray();
                    const double dx=m_target.x()-tank[0].toDouble(),dy=m_target.y()-tank[1].toDouble();
                    if(std::hypot(dx,dy)>2) {
                        const double angle=std::atan2(-dy,dx)*180.0/std::acos(-1.0);
                        const double delta=std::remainder(angle-tank[2].toDouble(),360.0);
                        buttons=std::abs(delta)>3?(delta>0?4:8):1;
                    }
                }
                if(m_pointerFire||m_fireQueued||(m_buttons&16))buttons|=16;
                QJsonObject input{{"type","input"},{"buttons",buttons},{"seq",++m_sequence},{"round",m_state["round"]},{"mouse",m_pointerActive}};
                if(m_pointerActive)input["target"]=QJsonArray{m_target.x(),m_target.y()};
                input["tick"]=m_sync.tick;input["sent"]=now;input["shot"]=m_shotSequence;input["arenaAck"]=m_joined?m_state["round"].toInt():0;m_sync.sent();packet(input);m_fireQueued=false;
            }
        }
        if(now-m_heard>1500) {m_status=m_joined?"等待房主连接恢复，对局已暂停。":"等待房主响应；邀请可能已经失效。";emit changed();}
    }
}
QString TankController::networkText() const {
    if(m_room.isEmpty())return {};
    const auto transport=m_service->gameTransport(m_peer);
    if(m_host)return transport+" · 房主裁决 · 状态约 25 次/秒";
    if(!m_peerNet2)return "兼容同步 · 双方更新可启用本地预测";
    return transport+" · "+(m_sync.rttMs()<0?QString("本地预测"):QString("对局往返 %1 ms · P95 %2 ms · 缓冲 %3 ms").arg(m_sync.rttMs()).arg(m_sync.p95Ms()).arg(m_sync.interpolationMs()));
}
void TankController::rebuildPrediction() {
    m_renderState=m_state;
    if(m_host||!m_joined||!m_peerNet2||m_state["handling"].toInt()!=2||m_paused||m_clock.elapsed()-m_heard>=250||m_state["wait"].toInt()||m_state["finished"].toBool())return;
    if(!m_prediction.loadMovementSnapshot(m_state))return;
    int begin=qMax(0,int(m_sync.pending.size())-10);
    for(int i=begin;i<m_sync.pending.size();++i){const auto& sample=m_sync.pending[i];
        const auto value=sample.value;
        if(value["mouse"].toBool()){auto t=value["target"].toArray();m_prediction.pointerInput(1,QPointF(t[0].toDouble(),t[1].toDouble()),false);}else m_prediction.input(1,value["buttons"].toInt()&15);
        m_prediction.predictMovement(1);
    }
    auto tanks=m_state["tanks"].toArray();auto own=tanks[1].toArray();const auto predicted=m_prediction.snapshot()["tanks"].toArray()[1].toArray();for(int j=0;j<3;++j)own[j]=predicted[j];tanks[1]=own;m_renderState["tanks"]=tanks;
    auto aim=m_state["aim"].toArray();aim[1]=m_prediction.snapshot()["aim"].toArray()[1];m_renderState["aim"]=aim;
}

}
