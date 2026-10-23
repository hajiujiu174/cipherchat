#include "gameudp.h"
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QtEndian>
namespace Messager {
GameUdp::GameUdp(QObject* parent):QObject(parent){
    m_clock.start();m_timer.setInterval(250);
    connect(&m_timer,&QTimer::timeout,this,&GameUdp::heartbeat);
    connect(&m_socket,&QUdpSocket::readyRead,this,&GameUdp::read);
}
void GameUdp::stop(){m_timer.stop();m_socket.close();m_user.clear();m_peers.clear();m_bound=-10000;}
void GameUdp::start(const QString& host,quint16 port,const QString& user){
    stop();m_sent=m_received=m_fallback=0;
    if(!port||user.isEmpty()||!m_host.setAddress(host))return;
    if(!m_socket.bind(m_host.protocol()==QAbstractSocket::IPv6Protocol?QHostAddress::AnyIPv6:QHostAddress::AnyIPv4,quint16(0)))return;
    m_user=user;m_port=port;m_timer.start();heartbeat();
}
bool GameUdp::write(QJsonObject data){
    if(!capable())return false;
    data["v"]=1;data["from"]=m_user;
    const auto bytes=QJsonDocument(data).toJson(QJsonDocument::Compact);
    return bytes.size()<=1200&&m_socket.writeDatagram(bytes,m_host,m_port)==bytes.size();
}
void GameUdp::advertise(const QString& peer,bool enabled){
    if(peer.isEmpty())return;
    if(!enabled){m_peers.remove(peer);return;}
    if(!m_peers.contains(peer)&&m_peers.size()>=32)m_peers.erase(m_peers.begin());
    m_peers[peer].active=m_clock.elapsed();
    write({{"op","probe"},{"to",peer}});
}
void GameUdp::heartbeat(){
    write({{"op","bind"}});
    const auto now=m_clock.elapsed();
    for(auto it=m_peers.begin();it!=m_peers.end();){
        if(now-it->active>10000){it=m_peers.erase(it);continue;}
        write({{"op","probe"},{"to",it.key()}});++it;
    }
}
bool GameUdp::ready(const QString& peer)const{
    const auto now=m_clock.elapsed();
    return capable()&&now-m_bound<1000&&m_peers.contains(peer)&&now-m_peers[peer].pong<900;
}
bool GameUdp::send(const QString& peer,const QJsonObject& data){
    if(m_peers.contains(peer))m_peers[peer].active=m_clock.elapsed();
    if(!ready(peer)){++m_fallback;return false;}
    const auto bytes=QJsonDocument(data).toJson(QJsonDocument::Compact);
    if(bytes.size()>32768){++m_fallback;return false;}
    const bool sent=write({{"op","data"},{"to",peer},{"blob",QString::fromLatin1(qCompress(bytes,1).toBase64())}});
    if(sent)++m_sent;else ++m_fallback;
    return sent;
}
void GameUdp::read(){
    // Bound work per event so malformed floods cannot monopolize the GUI loop.
    for(int budget=0;budget<128&&m_socket.hasPendingDatagrams();++budget){
        const auto d=m_socket.receiveDatagram(1201);
        if(d.senderAddress()!=m_host||d.senderPort()!=m_port||d.data().size()>1200)continue;
        const auto o=QJsonDocument::fromJson(d.data()).object();
        if(o["v"].toInt()!=1)continue;
        const auto op=o["op"].toString(),peer=o["from"].toString();
        if(op=="bound"){m_bound=m_clock.elapsed();continue;}
        if(!m_peers.contains(peer))continue;
        if(op=="probe"){write({{"op","pong"},{"to",peer}});continue;}
        if(op=="pong"){m_peers[peer].pong=m_clock.elapsed();continue;}
        if(op!="data")continue;
        const auto bytes=QByteArray::fromBase64(o["blob"].toString().toLatin1());
        if(bytes.size()<5)continue;
        const auto size=qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()));
        if(size>32768)continue;
        const auto decoded=qUncompress(bytes);if(decoded.size()!=size)continue;
        const auto data=QJsonDocument::fromJson(decoded).object();
        const auto game=data["game"].toString(),type=data["type"].toString();
        if((game!="tanktrouble"&&game!="badminton")||(type!="state"&&type!="input")||data["net"].toInt()!=2)continue;
        ++m_received;emit packet(peer,data);
    }
    if(m_socket.hasPendingDatagrams())QTimer::singleShot(0,this,&GameUdp::read);
}
QJsonObject GameUdp::stats()const{return {{"sent",m_sent},{"received",m_received},{"tls_fallback",m_fallback}};}
}
