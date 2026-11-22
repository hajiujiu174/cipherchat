// 处理收到的通话请求，并按业务结果回复当前连接。
void Router::handleCall(Peer& peer,const cc::proto::Message& msg) {
    const auto id=bodyStr(msg.body,"call_id"),action=bodyStr(msg.body,"action"),to=bodyStr(msg.body,"to");
    // 构造当前分支的失败结果或拒绝响应，供调用处统一返回。
    auto reject=[&](const char* reason){deliver(peer,cc::proto::withV2({cc::proto::MsgType::SocialResult,msg.msgId,{{"op","call"},{"ok",false},{"call_id",id},{"error",reason}}}));};
    if(id.empty()||id.size()>64||msg.body.dump().size()>64*1024){reject("invalid_request");return;}
    const auto now=clock_();
    for(auto it=calls_.begin();it!=calls_.end();)if(now-it->second.touched>(it->second.active?120000:30000)){
        for(const auto& user:{it->second.caller,it->second.callee})if(auto* target=findOnline(user))deliver(*target,cc::proto::withV2({cc::proto::MsgType::SocialResult,"",{{"op","call"},{"ok",true},{"action","end"},{"call_id",it->first},{"reason","timeout"}}}));it=calls_.erase(it);
    }else ++it;
    if(action=="invite"){
        const auto friends=db_.friendsForUser(peer.username());if(to==peer.username()||!findOnline(to)||db_.isBlocked(peer.username(),to)||std::find(friends.begin(),friends.end(),to)==friends.end()){reject("unavailable");return;}
        if(calls_.count(id)){reject("conflict");return;}
        for(const auto& [key,call]:calls_)if(call.caller==to||call.callee==to||call.caller==peer.username()||call.callee==peer.username()){reject("busy");return;}
        if(calls_.size()>=1024){reject("busy");return;}calls_[id]={peer.username(),to,false,now};
    }
    auto it=calls_.find(id);if(it==calls_.end()){reject("not_found");return;}auto& call=it->second;
    if(peer.username()!=call.caller&&peer.username()!=call.callee){reject("forbidden");return;}
    const auto destination=peer.username()==call.caller?call.callee:call.caller;
    if(action=="accept"){if(peer.username()!=call.callee||call.active){reject("forbidden");return;}call.active=true;}
    else if(action=="media"){
        if(!call.active){reject("forbidden");return;}
        auto& stream=call.streams[peer.username()];const auto sequence=msg.body.value("sequence",std::uint64_t{0});if(sequence<=stream.second||now-stream.first<25)return;stream={now,sequence};
    }else if(action!="invite"&&action!="end"&&action!="reject"){reject("invalid_request");return;}
    call.touched=now;auto body=msg.body;body["from"]=peer.username();body["ok"]=true;body.erase("to");
    if(auto* target=findOnline(destination))deliver(*target,cc::proto::withV2({cc::proto::MsgType::SocialResult,"",body}));else{reject("unavailable");calls_.erase(it);return;}
    if(action=="end"||action=="reject")calls_.erase(it);
}
