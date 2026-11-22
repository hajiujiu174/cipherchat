#include "server/router/router.hpp"
#include "server/router/validation.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace cc::server {

using cc::proto::bodyStr;

// 分派文件缓存的登记、写块、读取和取消操作，并返回结果。
void Router::handleFileCache(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer,"文件缓存") || !usesV2(peer)) return;
    using cc::proto::MsgType;
    const auto id = bodyStr(msg.body,"file_id"),op = bodyStr(msg.body,"op");
    cc::proto::Message response{MsgType::FileCacheResult,msg.msgId,{{"ok",false},{"op",op},{"file_id",id}}};
    try {
        cc::Bytes raw; if (id.size()>32 || !cc::base64Decode(id,raw) || raw.size()!=16) throw std::runtime_error("bad cache id");
        if (op=="begin") {
            const auto to=bodyStr(msg.body,"to"); if(!db_.findAccount(to)) throw std::runtime_error("bad recipient");
            const auto size=msg.body.at("size").get<std::uint64_t>(); const auto chunks=msg.body.at("chunks").get<std::uint32_t>();
            auto status=db_.fileCache().begin(peer.username(),id,to,size,chunks);
            response.body.update(status);response.body["ok"]=true;
        } else if(op=="put") {
            const auto seq=msg.body.at("seq").get<std::uint32_t>(); const auto data=bodyStr(msg.body,"data");
            if(data.size()>180*1024 || !cc::base64Decode(data,raw)) throw std::runtime_error("bad block");
            response.body["next"]=db_.fileCache().put(peer.username(),id,seq,raw);response.body["ok"]=true;
        } else if(op=="cancel") {
            db_.fileCache().cancel(peer.username(),id);response.body["ok"]=true;
        } else if(op=="get") {
            const auto owner=bodyStr(msg.body,"owner"); const auto start=msg.body.at("seq").get<std::uint32_t>();
            const auto count=msg.body.value("count",1u);if(count<1 || count>16 || start>16384-count)throw std::runtime_error("bad window");
            response.body["owner"]=owner;
            for(unsigned i=0;i<count;++i) {
                const auto data=db_.fileCache().get(peer.username(),owner,id,start+i);
                response.body["ok"]=true;response.body["seq"]=start+i;response.body["available"]=bool(data);
                if(data)response.body["data"]=cc::base64Encode(*data);else response.body.erase("data");
                deliver(peer,response);if(!data)break;
            }
            return;
        } else throw std::runtime_error("bad operation");
    } catch(const std::exception&) { response.body["ok"]=false;response.body.erase("data");response.body["error"]="cache_unavailable"; }
    deliver(peer,std::move(response));
}

// 处理收到的文件块进度确认请求，并按业务结果回复当前连接。
void Router::handleFileAck(Peer& peer, const cc::proto::Message& msg) {
    if (usesV2(peer)) {
        deliver(peer, cc::proto::makeError("protocol_error", "文件确认必须经过会话加密"));
        return;
    }
    if (!requireLogin(peer, "发送文件确认")) return;
    const std::string to = bodyStr(msg.body, "to");
    if (to.empty()) {
        deliver(peer, cc::proto::makeError("bad_target", "file_ack 缺少 to"));
        return;
    }
    // 服务器透传给原发送方：还原 msg_id 与 seq
    Peer* p = findOnline(to);
    if (p) {
        deliver(*p, cc::proto::makeFileAck(msg.msgId,
                                          cc::proto::bodyInt(msg.body, "seq")));
    }
    // 不在线时静默丢弃：文件续传由发送方超时重发
}

} // namespace cc::server
