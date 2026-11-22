#include "server/router/router.hpp"
#include "server/router/validation.hpp"

namespace cc::server {

// 校验游戏对端、好友关系及转发频率，将请求转发给在线接收者。
void Router::handleGameRelay(Peer& peer, const cc::proto::Message& msg) {
    if (!usesV2(peer) || peer.username().empty() || findOnline(peer.username())!=&peer || !gamePeers_.count(&peer) || !gamePeers_.at(&peer)) return;
    const auto to=cc::proto::bodyStr(msg.body,"to");
    if(to==peer.username() || !isValidUsername(to) || !msg.body.contains("data") ||
       !msg.body["data"].is_object() || msg.body["data"].dump().size()>8192) return;
    const auto now=clock_();
    if(auto i=lastGameRelay_.find(&peer);i!=lastGameRelay_.end() && now-i->second<20) return;
    lastGameRelay_[&peer]=now;
    auto friends=db_.friendsForUser(peer.username());
    if(std::find(friends.begin(),friends.end(),to)==friends.end()) return;
    if(auto* target=findOnline(to);target&&gamePeers_.count(target)&&gamePeers_.at(target)) deliver(*target,cc::proto::withV2({cc::proto::MsgType::GameRelay,"",
        {{"from",peer.username()},{"data",msg.body["data"]}}}));
}

} // namespace cc::server
