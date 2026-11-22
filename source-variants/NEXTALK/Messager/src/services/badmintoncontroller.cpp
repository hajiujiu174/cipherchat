#include "badmintoncontroller.h"
#include "chatservice.h"
#include "gomokuwire.h"
#include <QUuid>
#include <cmath>
namespace Messager {
BadmintonController::BadmintonController(ChatService* service,QObject* parent):QObject(parent),m_service(service) {
    m_clock.start();m_state=m_engine.snapshot();m_timer.setInterval(15);m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer,&QTimer::timeout,this,&BadmintonController::pump);m_timer.start();
    connect(service,&ChatService::gamePacket,this,&BadmintonController::receive);
    connect(service,&ChatService::sessionChanged,this,[this] {
        if(!m_practice&&!m_room.isEmpty() && (!m_service->online()||m_owner!=m_service->username()))clear("连接已断开，请重新邀请好友开局。");
        emit changed();
    });
    connect(service,&ChatService::gameRelayChanged,this,&BadmintonController::changed);
}
void BadmintonController::clear(const QString& text) {
    m_practice=false;m_sync.reset();m_renderState={};m_remoteInput={};m_peerNet2=false;m_paused=false;m_remoteTick=m_shotSequence=m_remoteShot=m_jumpSequence=m_remoteJump=m_swingSequence=m_remoteSwing=0;m_echo=-1;m_room.clear();m_peer.clear();m_run.clear();m_joined=false;stopInput();m_status=text;emit changed();
}
void BadmintonController::packet(QJsonObject data) {
    if(m_practice||m_room.isEmpty())return;
    data["net"]=(data["type"]=="input"&&!m_peerNet2)?1:2;data["room"]=m_room;data["game"]="badminton";data["v"]=1;data["run"]=m_run;
    m_service->sendGamePacket(m_peer,data);
}
bool BadmintonController::create(const QString& peer) {
    if(!m_room.isEmpty()) {m_status="请先退出当前房间。";emit changed();return false;}
    if(!m_service->gameRelaySupported()||!m_service->isFriend(peer)) {
        m_status="请登录已启用实时游戏的服务器，并选择在线好友。";emit changed();return false;
    }
    bool online=false;for(const auto& c:m_service->contacts())if(c.username==peer)online=c.online;
    if(!online) {m_status="好友当前不在线。";emit changed();return false;}
    m_owner=m_service->username();m_peer=peer;m_room=QUuid::createUuid().toString(QUuid::Id128);
    m_run=QUuid::createUuid().toString(QUuid::Id128);m_host=true;m_joined=false;m_practice=false;m_lastFrame=-1;m_sequence=0;m_remoteSequence=-1;
    m_sync.reset();m_renderState={};m_remoteInput={};m_peerNet2=false;m_paused=false;m_remoteTick=m_shotSequence=m_remoteShot=m_jumpSequence=m_remoteJump=m_swingSequence=m_remoteSwing=0;m_echo=-1;
    stopInput();m_engine.reset();m_state=m_engine.snapshot();m_heard=m_clock.elapsed();
    const auto conversation=m_service->openDirectConversation(peer);
    if(conversation.isEmpty() || m_service->send(conversation,GomokuWire::encode({{"game","badminton"},{"type","invite"},{"room",m_room},{"black",m_owner},{"white",peer},{"run",m_run}})).isEmpty()) {
        clear("邀请发送失败，请稍后重试。");return false;
    }
    m_status="邀请已发送，等待好友加入。你是左侧紫方。";emit changed();emit chatRequested(peer);return true;
}
bool BadmintonController::openInvitation(const Message& message) {
    const auto o=GomokuWire::decode(message.text);
    if(o.isEmpty()||o["game"]!="badminton"||o["type"]!="invite"||message.outgoing||
       o["black"].toString()!=message.sender||o["white"].toString()!=m_service->username()||!m_service->isFriend(message.sender))return false;
    static const QRegularExpression id("^[0-9a-f]{32}$");
    if(!id.match(o["run"].toString()).hasMatch())return false;
    if(!m_room.isEmpty()) {m_status="请先退出当前房间再加入邀请。";emit changed();return false;}
    if(!m_service->gameRelaySupported()) {m_status="当前服务器尚未启用实时游戏，需要升级服务器后联机。";emit changed();return false;}
    m_owner=m_service->username();m_peer=message.sender;m_room=o["room"].toString();m_run=o["run"].toString();
    m_sync.reset();m_renderState={};m_peerNet2=false;m_paused=false;m_host=false;m_joined=false;m_practice=false;m_sequence=0;m_remoteSequence=-1;m_lastFrame=-1;stopInput();m_heard=m_clock.elapsed();m_sent=-1000;
    m_status="正在加入好友的房间……";emit changed();return true;
}
void BadmintonController::leave() {
    const QJsonObject data{{"type","leave"},{"room",m_room},{"run",m_run},{"game","badminton"},{"v",1}};
    const auto peer=m_peer;
    packet({{"type","leave"}});
    // Repeat control once after the server's per-connection rate window.
    if(!m_practice&&!m_room.isEmpty())QTimer::singleShot(80,m_service,[service=m_service,peer,data]{service->sendGamePacket(peer,data);});
    clear("已退出房间，可以重新邀请好友。");
}
void BadmintonController::setButtons(int buttons) {
    buttons&=15;const int pressed=buttons&~m_buttons;if(pressed&4)++m_jumpSequence;if(pressed&8)++m_swingSequence;m_actions|=pressed&12;m_buttons=buttons;
}
void BadmintonController::stopInput() {m_buttons=0;m_actions=0;}
void BadmintonController::startPractice() {
    if(!m_room.isEmpty())return;
    m_practice=true;m_host=true;m_joined=true;m_engine.reset();m_state=m_engine.snapshot();
    m_room=QUuid::createUuid().toString(QUuid::Id128);m_owner=m_service->username();stopInput();
    m_status="单人练习 · 你是左侧紫方，先得 7 分获胜";emit changed();
}
void BadmintonController::receive(const QString& peer,const QJsonObject& p) {
    if(m_practice||m_room.isEmpty()||peer!=m_peer||p["room"]!=m_room||p["run"]!=m_run||p["v"].toInt()!=1||p["game"]!="badminton")return;
    const auto type=p["type"].toString();
    if(type=="leave") {clear("好友已离开房间。");return;}
    if(m_host) {
        if(type=="join") {m_peerNet2=p["net"].toInt()==2;m_joined=true;m_heard=m_clock.elapsed();m_status="对战中 · 你是左侧紫方";emit changed();}
        else if(type=="input"&&m_joined) {
            const auto seq=p["seq"].toInt(-1),b=p["buttons"].toInt(-1);
            if(seq<=m_remoteSequence||seq>1000000000||b<0||b>15)return;
            if(p["round"].toInt(-1)!=m_state["round"].toInt())return;
            if(p["net"].toInt()==2)for(const auto key:{"jump","swing"})if(!p[key].isDouble()||p[key].toDouble()!=p[key].toInt(-1)||p[key].toInt()<0||p[key].toInt()>1000000000)return;
            if(p["net"].toInt()==2&&(p["jump"].toInt()<m_remoteJump||p["swing"].toInt()<m_remoteSwing))return;
            if(p["net"].toInt()==2) {
                if(!p["tick"].isDouble()||p["tick"].toDouble()!=p["tick"].toInt(-1)||p["tick"].toInt(-1)<m_remoteTick||p["tick"].toInt()>1000000000)return;
                m_remoteTick=p["tick"].toInt();m_echo=p["sent"].toInteger(-1);
            }
            if(p["net"].toInt()==2){m_engine.input(1,b&3);int actions=0;if(p["jump"].toInt()>m_remoteJump)actions|=4;if(p["swing"].toInt()>m_remoteSwing)actions|=8;m_engine.queueActions(1,actions);m_remoteJump=p["jump"].toInt();m_remoteSwing=p["swing"].toInt();}
            else m_engine.input(1,b);
            m_remoteSequence=seq;m_heard=m_inputAt=m_clock.elapsed();
        }
    } else if(type=="state") {
        const auto s=p["state"].toObject();
        if(!BadmintonEngine::validSnapshot(s)||s["frame"].toInt()<m_lastFrame)return;
        if(m_joined&&m_state["round"]!=s["round"])stopInput();
        const auto previousView=predictedState();
        const bool modern=p["net"].toInt()==2;
        if(modern&&!m_sync.validAck(p["ackTick"]))return;
        if(m_state["round"]!=s["round"]||p["paused"].toBool())m_sync.clearPrediction();
        m_peerNet2=modern;m_paused=p["paused"].toBool();
        if(modern)m_sync.snapshot(m_clock.elapsed(),p["ackTick"].toInt(),p["echo"].toInteger(-1));
        m_lastFrame=s["frame"].toInt();m_state=s;m_heard=m_clock.elapsed();m_joined=true;
        rebuildPrediction();
        if(modern&&previousView["round"]==m_renderState["round"]){auto a=previousView["players"].toArray()[1].toArray(),b=m_renderState["players"].toArray()[1].toArray();const double distance=std::hypot(a[0].toDouble()-b[0].toDouble(),a[1].toDouble()-b[1].toDouble());if(distance>22)m_sync.corrected(distance);}
        m_status=p["paused"].toBool()?"连接暂时中断，双方已暂停。":m_state["finished"].toBool()?"本场结束 · 先得 7 分获胜，请退出后重新邀请。":"对战中 · 你是右侧绿方";emit changed();
    }
}
void BadmintonController::pump() {
    if(m_room.isEmpty())return;
    const auto now=m_clock.elapsed();
    if(!m_practice&&(!m_service->online()||!m_service->gameRelaySupported())) {clear("连接已断开，请重新邀请好友开局。");return;}
    if(m_host&&m_joined) {
        const bool paused=!m_practice&&now-m_heard>1500;
        m_engine.input(0,paused?0:m_buttons|m_actions);m_actions=0;
        if(m_practice)m_engine.computerInput();else if(now-m_inputAt>350)m_engine.input(1,0);
        if(!paused)m_engine.step();
        auto next=m_engine.snapshot();if(next["round"]!=m_state["round"])stopInput();m_state=next;
        m_status=paused?"好友连接中断，对局已暂停，等待恢复。":m_state["finished"].toBool()?"本场结束 · 先得 7 分获胜，请退出后重新开始。":m_practice?"单人练习 · 你是左侧紫方":"对战中 · 你是左侧紫方";
        if(!m_practice&&now-m_sent>=40) {m_sent=now;packet({{"type","state"},{"state",m_state},{"paused",paused},{"ackTick",m_remoteTick},{"echo",m_echo}});}
        emit changed();
    } else if(!m_host) {
        if(m_joined&&m_peerNet2&&!m_paused&&now-m_heard<250){m_sync.record({{"buttons",(m_buttons&3)|m_actions}});m_actions=0;rebuildPrediction();emit changed();}
        if(now-m_sent>=(m_joined?40:500)) {
            m_sent=now;
            if(!m_joined)packet({{"type","join"}});
            else {m_sync.sent();packet({{"type","input"},{"buttons",m_buttons|m_actions},{"seq",++m_sequence},{"round",m_state["round"]},{"tick",m_sync.tick},{"sent",now},{"jump",m_jumpSequence},{"swing",m_swingSequence}});if(!m_peerNet2)m_actions=0;}
        }
        if(now-m_heard>1500) {m_status=m_joined?"等待房主连接恢复，对局已暂停。":"等待房主响应；邀请可能已经失效。";emit changed();}
    }
}
QString BadmintonController::networkText() const {
    if(m_room.isEmpty() || m_practice)return {};
    const auto transport=m_service->gameTransport(m_peer);
    if(m_host)return transport+" · 房主裁决 · 状态约 25 次/秒";
    if(!m_peerNet2)return "兼容同步 · 双方更新可启用本地预测";
    return transport+" · "+(m_sync.rttMs()<0?QString("本地预测"):QString("对局往返 %1 ms · P95 %2 ms · 缓冲 %3 ms").arg(m_sync.rttMs()).arg(m_sync.p95Ms()).arg(m_sync.interpolationMs()));
}
void BadmintonController::rebuildPrediction() {
    m_renderState=m_state;
    if(m_host||!m_joined||!m_peerNet2||m_paused||m_clock.elapsed()-m_heard>=250||m_state["wait"].toInt()||m_state["finished"].toBool())return;
    if(!m_prediction.loadSnapshot(m_state))return;
    int begin=qMax(0,int(m_sync.pending.size())-13);
    for(int i=begin;i<m_sync.pending.size();++i){const auto& sample=m_sync.pending[i];
        m_prediction.input(1,sample.value["buttons"].toInt());m_prediction.step();
    }
    auto players=m_state["players"].toArray();players[1]=m_prediction.snapshot()["players"].toArray()[1];m_renderState["players"]=players;m_renderState["ball"]=m_prediction.snapshot().value("ball");
}

}
