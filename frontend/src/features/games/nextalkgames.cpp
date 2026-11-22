#include "services/nextalkservice.h"
#include "features/games/gomokuwire.h"

namespace Messager {

// 为允许联机的好友选择游戏传输路径，实时状态在 UDP 可用时优先使用 UDP 中转。
bool NextalkService::sendGamePacket(const QString& peer,const QJsonObject& packet) {
    if(!gameRelaySupported() || !isFriend(peer))return false;
    auto data=packet;
    const bool actionGame=data["game"]=="tanktrouble"||data["game"]=="badminton";
    if(actionGame){
        data["udp"]=m_udp.capable()?1:0;
        if(data["net"].toInt()==2&&(data["type"]=="input"||data["type"]=="state")&&m_udp.send(peer,data))return true;
    }
    return m_gateway.post({{"command","gameRelay"},{"value",QJsonObject{{"to",peer},{"data",data}}}});
}

} // namespace Messager
