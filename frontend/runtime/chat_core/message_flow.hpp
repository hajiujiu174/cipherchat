// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_MESSAGE_FLOW_HPP
#define NEXTALK_CHAT_MESSAGE_FLOW_HPP

#include <string>

namespace nextalk {
namespace chat {

enum SendOutcome { SaveFailed, SavedForRetry, TransportQueued };
enum ReceiveOutcome { ReceiveFailed, NonText, DuplicateText, TextSaved };

// 调用应用层保存发件箱和提交网络数据。
class OutgoingText {
public:
    virtual ~OutgoingText() {}
    virtual bool saveToOutbox() = 0;
    virtual bool submitSavedPacket() = 0;
};

// 调用应用层保存接收记录和回送签收。
class IncomingText {
public:
    virtual ~IncomingText() {}
    virtual ReceiveOutcome saveToHistory() = 0;
    virtual void acknowledge() = 0;
};

// 在读取本地状态前检查收件人、文本长度及群 ID。
bool validTextRequest(const std::string& recipient, const std::string& text, bool validGroup);

// 发件箱保存成功后才提交网络。网络失败时发件箱保留记录。
SendOutcome sendText(OutgoingText& text);

// 新保存的文本和已经处理过的包都可以回送签收。
ReceiveOutcome receiveText(IncomingText& text);

} // namespace chat
} // namespace nextalk

#endif
