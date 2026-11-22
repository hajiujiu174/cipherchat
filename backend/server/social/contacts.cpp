#include "server/router/router.hpp"
#include "server/router/validation.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include "common/crypto/crypto.hpp"
#include <cstdlib>

namespace cc::server {

using cc::proto::bodyStr;

// 处理收到的LookupUID请求，并按业务结果回复当前连接。
void Router::handleLookupUid(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "查询 UID")) return;
    const auto username = bodyStr(msg.body, "username");
    if (!usesV2(peer) || !isValidUsername(username)) { deliver(peer, cc::proto::makeError("bad_uid_query", "UID 查询账号无效")); return; }
    const auto account = db_.findAccount(username);
    auto response = cc::proto::makeUidResult(username, account ? account->id : 0); response.msgId = msg.msgId;
    deliver(peer, std::move(response));
}

// 按操作分派好友、群管理、资料、应用同步、通话或 UDP 票据请求。
void Router::handleSocial(Peer& peer, const cc::proto::Message& msg) {
    if(!requireLogin(peer,"个人资料")||!usesV2(peer))return;
    if(bodyStr(msg.body,"op")=="call"){handleCall(peer,msg);return;}
    if(bodyStr(msg.body,"op")=="udp_ticket"){
        const char* secret=std::getenv("NEXTALK_GAME_UDP_SECRET");
        if(!secret||std::strlen(secret)<32){deliver(peer,cc::proto::withV2({cc::proto::MsgType::SocialResult,msg.msgId,{{"op","udp_ticket"},{"ok",false}}}));return;}
        const auto expires=std::time(nullptr)+300;const auto session=cc::base64Encode(cc::crypto::randomBytes(16));
        const auto key=cc::crypto::hmacSha256(secret,peer.username()+"\n"+std::to_string(expires)+"\n"+session);
        deliver(peer,cc::proto::withV2({cc::proto::MsgType::SocialResult,msg.msgId,{{"op","udp_ticket"},{"ok",true},{"expires",expires},{"session",session},{"key",cc::base64Encode(key)}}}));return;
    }
    cc::proto::Message response{cc::proto::MsgType::SocialResult,msg.msgId,{{"ok",false},{"op",bodyStr(msg.body,"op")},{"username",bodyStr(msg.body,"username",peer.username())}}};
    response.body["view_id"]=bodyStr(msg.body,"view_id");
    try { const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();response.body=db_.socialRequest(peer.username(),msg.body,now); }
    catch(const std::exception& error) {
        const std::string reason=error.what();const std::vector<std::string> known={"forbidden","not_found","invalid_user","invalid_text","empty_text","text_too_long","invalid_avatar","invalid_cursor","quota_reached","conflict","invalid_request"};
        response.body["error"]=std::find(known.begin(),known.end(),reason)!=known.end()?reason:"invalid_request";
    }
    if(response.body.value("ok",false) && response.body.contains("notify_users")) {
        for(const auto& name:response.body["notify_users"])if(auto* target=findOnline(name.get<std::string>())) {
            deliverDirectory(*target);
            if(response.body.contains("group")) {
                const auto& g=response.body["group"];auto members=g.at("members").get<std::vector<std::string>>();
                if(std::find(members.begin(),members.end(),target->username())==members.end())members.clear();
                auto update=cc::proto::makeGroupUpdate(g.at("id").get<std::int64_t>(),g.at("name").get<std::string>(),members);update.body["revision"]=g.at("revision");deliver(*target,std::move(update));
            }
        }
        response.body.erase("notify_users");
    }
    deliver(peer,std::move(response));
}

// 处理收到的列表用户请求，并按业务结果回复当前连接。
void Router::handleListUsers(Peer& peer) {
    if (!requireLogin(peer, "获取用户列表")) return;
    std::vector<std::pair<std::string, bool>> users;
    for (const std::string& name : db_.allUsernames()) {
        // onMessage 已持有路由锁，不再次调用会加锁的公开查询。
        users.emplace_back(name, findOnline(name) != nullptr);
    }
    deliver(peer, cc::proto::makeUserList(users));
}

// 投递目录。
void Router::deliverDirectory(Peer& peer) {
    if (!directories_.count(&peer)) return; // Old clients do not understand contact_list.
    std::vector<std::pair<std::string, bool>> users;
    for (const auto& name : db_.friendsForUser(peer.username())) users.emplace_back(name, findOnline(name) != nullptr);
    auto response = cc::proto::makeContactList(users);
    response.body["incoming"] = db_.friendRequests(peer.username(), true);
    response.body["outgoing"] = db_.friendRequests(peer.username(), false);
    deliver(peer, std::move(response));
}

// 处理收到的列表联系人请求，并按业务结果回复当前连接。
void Router::handleListContacts(Peer& peer) {
    if (!requireLogin(peer, "获取联系人")) return;
    directories_[&peer] = true;
    deliverDirectory(peer);
}

// 处理收到的搜索用户请求，并按业务结果回复当前连接。
void Router::handleSearchUsers(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "查询陌生人")) return;
    const auto query = bodyStr(msg.body, "query");
    const auto after = bodyStr(msg.body, "after");
    if (!msg.body.contains("query") || !msg.body["query"].is_string() || query.size() > 32 || after.size() > 32 ||
        (msg.body.contains("after") && !msg.body["after"].is_string())) {
        deliver(peer, cc::proto::makeError("invalid_argument", "查询格式无效")); return;
    }
    std::vector<std::pair<std::string, bool>> users;
    for (const auto& name : db_.searchUsernames(peer.username(), query, after)) users.emplace_back(name, findOnline(name) != nullptr);
    const bool more = users.size() > 50; if (more) users.resize(50);
    auto response = cc::proto::makeSearchResult(users); response.msgId = msg.msgId;
    response.body["query"] = query; response.body["after"] = after;
    response.body["next"] = more ? users.back().first : "";
    deliver(peer, std::move(response));
}

// 处理收到的Add好友请求，并按业务结果回复当前连接。
void Router::handleAddFriend(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "添加好友")) return;
    const auto username = bodyStr(msg.body, "username");
    if (username.empty() || username.size() > 32 || username == peer.username()) {
        deliver(peer, cc::proto::makeFriendResult(false, "用户名无效", username)); return;
    }
    const auto state = db_.requestFriend(peer.username(), username);
    if (state.empty()) {
        deliver(peer, cc::proto::makeFriendResult(false, "用户不存在或待处理申请过多", username)); return;
    }
    auto response = cc::proto::makeFriendResult(true, "", username); response.body["state"] = state;
    deliver(peer, std::move(response));
    directories_[&peer] = true;
    deliverDirectory(peer);
    if (auto* other = findOnline(username)) deliverDirectory(*other);
}

// 处理收到的Respond好友请求，并按业务结果回复当前连接。
void Router::handleRespondFriend(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "处理好友申请")) return;
    const auto username = bodyStr(msg.body, "username");
    if (username.empty() || username.size() > 32 || !msg.body.contains("accept") || !msg.body["accept"].is_boolean()) {
        deliver(peer, cc::proto::makeFriendResult(false, "申请格式无效", username)); return;
    }
    const bool accept = cc::proto::bodyBool(msg.body, "accept");
    if (!db_.respondFriend(peer.username(), username, accept)) {
        deliver(peer, cc::proto::makeFriendResult(false, "该好友申请不存在或已处理", username)); return;
    }
    auto response = cc::proto::makeFriendResult(true, "", username); response.body["state"] = accept ? "accepted" : "declined";
    deliver(peer, std::move(response));
    directories_[&peer] = true; deliverDirectory(peer);
    if (auto* other = findOnline(username)) deliverDirectory(*other);
}

} // namespace cc::server
