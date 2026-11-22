#pragma once
#include "ccfw/session_service.hpp"
#include "ccfw/transport.hpp"

namespace ccfw {
class IChatService {
public:
    virtual ~IChatService() = default;
    // 返回本地消息 ID 仅代表进入待发流程，不代表对端已收到
    virtual Result<std::string> sendText(const SendText&) = 0;
};
class ChatService final : public IChatService {
public:
    ChatService(ITransport& net, ISessionService& sessions, ILocalStore& store)
        : net_(net), sessions_(sessions), store_(store) {}
    Result<std::string> sendText(const SendText&) override;
    Result<> handleBundle(const cc::proto::PrekeyBundle&);
    Result<ChatRecord> receive(const WireEnvelope&);
    Result<> handleAck(const std::string& messageId, DeliveryState, const std::string& peer = {});
    Result<> flushOutbox(); // 重连只重发持久化密文，不能重新 encrypt 同一条消息
    Result<> retryPending(const std::string& peer);
    Result<PeerIdentity> offeredIdentity(const std::string& peer) const;
    std::vector<ChatRecord> takeEvents();
private:
    ITransport& net_;
    ISessionService& sessions_;
    ILocalStore& store_;
    std::map<std::string, cc::proto::PrekeyBundle> bundles_;
    std::map<std::string, std::vector<WireEnvelope>> incoming_;
    std::vector<ChatRecord> events_;
};
} // namespace ccfw
