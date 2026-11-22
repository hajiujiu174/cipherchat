// 人工实现，由 ChatGPT 规范化。

#include "message_flow.hpp"

namespace nextalk {
namespace chat {

// 检查消息是否达到文本发送入口的基本条件。
bool validTextRequest(const std::string& recipient, const std::string& text, bool validGroup) {
    return !recipient.empty() && recipient.size() <= 32 &&
           !text.empty() && text.size() <= 65536 && validGroup;
}

// 本地持久化成功后才调用传输层提交。
SendOutcome sendText(OutgoingText& text) {
    if (!text.saveToOutbox()) return SaveFailed;
    if (!text.submitSavedPacket()) return SavedForRetry;
    return TransportQueued;
}

// 接收失败时直接返回，已经保存过的消息可重复签收。
ReceiveOutcome receiveText(IncomingText& text) {
    const ReceiveOutcome outcome = text.saveToHistory();
    if (outcome == TextSaved || outcome == DuplicateText) text.acknowledge();
    return outcome;
}

} // namespace chat
} // namespace nextalk
