#include "server/router/router.hpp"
#include "server/router/validation.hpp"
#include "relay_flow.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace cc::server {

using cc::proto::bodyStr;
namespace classic = nextalk::chat;

// 读取当前账号待投递的消息，按在途容量补发并等待客户端签收。
void Router::deliverOfflineQueue(Peer& peer) {
    if (usesV2(peer)) {
        auto queued = db_.deliveries().peek(peer.username(), 64);
        if (!queued) throw std::runtime_error("delivery queue unavailable");
        struct Delivery final : classic::QueuedDeliveryPort {
            Router& router;
            Peer& receiver;
            Delivery(Router& router, Peer& receiver) : router(router), receiver(receiver) {}
            void deliverStored(const classic::PendingMessage& entry) override {
                auto wire = cc::proto::deserialize(entry.payload);
                if (!wire || !cc::proto::isV2(*wire)) throw std::runtime_error("invalid stored delivery");
                router.deliver(receiver, *wire);
            }
        } delivery(*this, peer);
        std::vector<classic::PendingMessage> pending;
        pending.reserve(queued.value().size());
        for (auto& item : queued.value()) {
            auto& entry = pending.emplace_back();
            entry.recipient = std::move(item.recipient);
            entry.sender = std::move(item.sender);
            entry.messageId = std::move(item.messageId);
            entry.payload = std::move(item.ciphertextJson);
        }
        classic::pumpRelayQueue(pending, inFlight_[&peer], delivery);
        return;
    }
    auto acc = db_.findAccount(peer.username());
    if (!acc) return;
    for (const auto& m : db_.drainOffline(acc->id)) {
        auto queued = cc::proto::deserialize(m.payloadJson);
        if (queued) deliver(peer, *queued);
    }
}

// 使用登录连接确定发件人，校验收件和群聊权限，再持久化服务端待投递记录。
void Router::handleSendMsg(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "发送消息")) return;
    const std::string to = bodyStr(msg.body, "to");
    if (usesV2(peer)) {
        struct Submit final : classic::RelaySubmitPort {
            Router& router;
            Peer& sender;
            Submit(Router& router, Peer& sender) : router(router), sender(sender) {}
            bool accountExists(const std::string& user) override {
                return router.db_.findAccount(user).has_value();
            }
            bool isBlocked(const classic::RelayRequest& request) override {
                return router.db_.isBlocked(request.sender, request.recipient);
            }
            bool groupAllows(const classic::RelayRequest& request) override {
                ccfw::server::GroupAuthorizer authorizer(router.db_);
                return authorizer.authorize(request.groupId, request.sender, request.recipient).ok();
            }
            classic::QueueStatus persist(const classic::RelayRequest& request) override {
                // v2 外层时间固定为 0，以便重发时序列化出的内容相同。
                const auto message = cc::proto::withV2(cc::proto::makeRecvMsg(
                    request.messageId, request.sender, request.recipient, request.kind, request.payload, 0, request.groupId));
                const auto saved = router.db_.deliveries().enqueue(
                    {request.recipient, request.sender, request.messageId, cc::proto::serialize(message)});
                if (saved) return classic::QueueOk;
                return saved.error().code == ccfw::ErrorCode::AlreadyProcessed
                    ? classic::QueueAlreadyProcessed : classic::QueueStorageError;
            }
            void reply(const classic::RelayRequest& request, classic::RelayReply result) override {
                using cc::proto::makeError;
                switch (result) {
                case classic::RelayBadTarget:
                    router.deliver(sender, makeError("bad_target", "非法的消息接收方"));
                    break;
                case classic::RelayNoUser:
                    router.deliver(sender, makeError("no_user", "用户不存在: " + request.recipient));
                    break;
                case classic::RelayBadFields:
                    router.deliver(sender, makeError("protocol_error", "消息字段非法"));
                    break;
                case classic::RelayBlocked:
                    router.deliver(sender, makeError("forbidden", "对方拒绝接收消息"));
                    break;
                case classic::RelayForbiddenGroup:
                    router.deliver(sender, makeError("forbidden", "无权向该群成员投递"));
                    break;
                case classic::RelayRejected: {
                    auto error = makeError("queue_rejected", "无法保存消息，消息标识冲突或队列已满");
                    error.msgId = request.messageId;
                    router.deliver(sender, std::move(error));
                    break;
                }
                case classic::RelayAlreadyReceived: {
                    auto ack = cc::proto::makeRecipientAck(request.messageId, request.sender);
                    ack.body["from"] = request.recipient;
                    router.deliver(sender, std::move(ack));
                    break;
                }
                case classic::RelayAccepted: {
                    auto ack = cc::proto::makeMsgAck(request.messageId, false);
                    ack.body["accepted"] = true;
                    ack.body["to"] = request.recipient;
                    router.deliver(sender, std::move(ack));
                    break;
                }
                }
            }
            void deliverOnline(const std::string& recipient) override {
                if (Peer* receiver = router.findOnline(recipient); receiver && router.usesV2(*receiver)) {
                    router.deliverOfflineQueue(*receiver);
                }
            }
        } submit(*this, peer);
        const auto group = msg.body.find("group_id");
        const bool validGroup = group == msg.body.end() ||
            (group->is_number_integer() && *group >= 0 && *group <= INT64_MAX);
        classic::RelayRequest request;
        request.sender = peer.username();
        request.recipient = to;
        request.messageId = msg.msgId;
        request.kind = bodyStr(msg.body, "kind");
        request.payload = bodyStr(msg.body, "payload");
        request.validGroupField = validGroup;
        request.groupId = validGroup ? cc::proto::bodyInt(msg.body, "group_id") : 0;
        classic::submitRelay(request, submit);
        return;
    }
    if (to.empty() || to == peer.username()) {
        deliver(peer, cc::proto::makeError("bad_target", "非法的消息接收方"));
        return;
    }
    if (!db_.findAccount(to)) {
        deliver(peer, cc::proto::makeError("no_user", "用户不存在: " + to));
        return;
    }
    if (Peer* receiver = findOnline(to); receiver && usesV2(*receiver)) {
        deliver(peer, cc::proto::makeError("protocol_error", "对端使用不同协议版本"));
        return;
    }
    const bool online = routeToUser(
        to, cc::proto::makeRecvMsg(msg.msgId, peer.username(), to,
                                   bodyStr(msg.body, "kind"),
                                   bodyStr(msg.body, "payload"),
                                   std::time(nullptr),
                                   cc::proto::bodyInt(msg.body, "group_id")));
    deliver(peer, cc::proto::makeMsgAck(msg.msgId, online));
}

// 按登录接收者和消息 ID 验证签收，更新投递队列并通知在线发件人。
void Router::handleRecipientAck(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "签收消息")) return;
    struct Ack final : classic::RelayAckPort {
        Router& router;
        Peer& receiver;
        Ack(Router& router, Peer& receiver) : router(router), receiver(receiver) {}
        bool findPending(const std::string& id, classic::PendingMessage& message) override {
            const auto found = router.db_.deliveries().find(receiver.username(), id);
            if (!found) throw std::runtime_error("delivery lookup failed");
            if (!found.value()) return false;
            message.sender = found.value()->sender;
            message.messageId = id;
            return true;
        }
        void persistAck(const std::string& id) override {
            if (!router.db_.deliveries().acknowledge(receiver.username(), id)) {
                throw std::runtime_error("acknowledgement failed");
            }
        }
        void rejectAck(classic::RelayAckError error) override {
            if (error == classic::RelayBadAck) {
                router.deliver(receiver, cc::proto::makeError("protocol_error", "签收字段非法"));
            } else {
                router.deliver(receiver, cc::proto::makeError("forbidden", "签收目标不匹配"));
            }
        }
        void releaseInFlight(const std::string& id) override {
            router.inFlight_[&receiver].erase(id);
        }
        void notifySender(const classic::PendingMessage& message) override {
            if (Peer* sender = router.findOnline(message.sender); sender && router.usesV2(*sender)) {
                auto ack = cc::proto::makeRecipientAck(message.messageId, message.sender);
                ack.body["from"] = receiver.username();
                router.deliver(*sender, std::move(ack));
            }
        }
        void continueDelivery() override {
            router.deliverOfflineQueue(receiver);
        }
    } ack(*this, peer);
    classic::receiveRelayAck(msg.msgId, bodyStr(msg.body, "to"), ack);
}

// 处理收到的正在输入请求，并按业务结果回复当前连接。
void Router::handleTyping(Peer& peer, const cc::proto::Message& msg) {
    if (!requireLogin(peer, "发送输入提示")) return;
    const std::string to = bodyStr(msg.body, "to");
    if (to.empty() || to.size() > 32 || to == peer.username()) return;
    if (usesV2(peer)) {
        const auto now = clock_(); const auto key = std::make_pair(&peer, to); const auto last = lastTyping_.find(key);
        if (last != lastTyping_.end() && now - last->second < 1000) return;
        std::erase_if(lastTyping_, [&](const auto& entry) { return now - entry.second >= 3000; });
        if (lastTyping_.size() >= 4096) return;
        lastTyping_[key] = now;
    }
    if (Peer* p = findOnline(to)) deliver(*p, cc::proto::makeTypingFrom(peer.username()));
}

} // namespace cc::server
