#pragma once
#include "ccfw/session_service.hpp"
#include "ccfw/transport.hpp"
#include <deque>
#include <chrono>

namespace ccfw {
class IChatService {
public:
    // 销毁chat_service 模块时释放它持有的资源。
    virtual ~IChatService() = default;
    // 返回本地消息 ID 仅代表进入待发流程，不代表对端已收到
    virtual Result<std::string> sendText(const SendText&) = 0;
};
class ChatService final : public IChatService {
public:
    // 构造聊天服务，初始化后续操作需要的状态或依赖。
    ChatService(ITransport& net, ISessionService& sessions, ILocalStore& store)
        : net_(net), sessions_(sessions), store_(store) {}
    // 校验消息内容和标识，将消息和发件箱持久化后再尝试网络发送。
    Result<std::string> sendText(const SendText&) override;
    // 校验入站聊天消息并保存本地历史，落盘成功后才向对端返回签收。
    Result<ChatRecord> receive(const WireEnvelope&);
    // 按服务端受理或收件方签收更新本机记录，收件方签收后清除对应待发项。
    Result<> handleAck(const std::string& messageId, DeliveryState, const std::string& peer = {});
    // 从持久化发件箱取出尚未完成的消息，在连接可用时批量重发。
    Result<> flushOutbox(); // 重连只重发持久化密文，不能重新 encrypt 同一条消息
    // 重试指定对端仍在待发队列中的有效消息。
    Result<> retryPending(const std::string& peer);
    // 取出事件。
    std::vector<ChatRecord> takeEvents();
    // 取出阶段事件。
    std::vector<MessageStageEvent> takeStageEvents();
    // 返回聊天服务使用的状态阶段。
    std::vector<MessageStageEvent> stages() const;
private:
    // 记录聊天服务。
    void observe(const std::string&, MessageStage, int errorCode = -1, bool retry = false);
    ITransport& net_;
    ISessionService& sessions_;
    ILocalStore& store_;
    std::map<std::string, cc::proto::PrekeyBundle> bundles_;
    std::map<std::string, std::vector<WireEnvelope>> incoming_;
    std::vector<ChatRecord> events_;
    struct Trace { std::chrono::steady_clock::time_point start; unsigned retries = 0; std::vector<MessageStageEvent> events; };
    std::map<std::string, Trace> traces_;
    std::deque<std::string> traceOrder_;
    std::vector<MessageStageEvent> stageEvents_;
};
} // namespace ccfw
