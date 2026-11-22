// 人工实现，由 ChatGPT 规范化。

#include "message_flow.hpp"

#include <iostream>
#include <string>

namespace {
using namespace nextalk::chat;

class Sender : public OutgoingText {
public:
    Sender(bool saved, bool submitted) : saved_(saved), submitted_(submitted) {}
    bool saveToOutbox() {
        steps += 'S';
        return saved_;
    }
    bool submitSavedPacket() {
        steps += 'T';
        return submitted_;
    }
    std::string steps;
private:
    bool saved_;
    bool submitted_;
};

class Receiver : public IncomingText {
public:
    explicit Receiver(ReceiveOutcome outcome) : outcome_(outcome) {}
    ReceiveOutcome saveToHistory() {
        steps += 'S';
        return outcome_;
    }
    void acknowledge() { steps += 'A'; }
    std::string steps;
private:
    ReceiveOutcome outcome_;
};

bool checkSend(bool saved, bool submitted, SendOutcome expected, const char* steps) {
    Sender sender(saved, submitted);
    return sendText(sender) == expected && sender.steps == steps;
}

bool checkReceive(ReceiveOutcome outcome, const char* steps) {
    Receiver receiver(outcome);
    return receiveText(receiver) == outcome && receiver.steps == steps;
}
} // namespace

int main() {
    if (!validTextRequest("bob", std::string(65536, 'a'), true) ||
        validTextRequest("bob", std::string(65537, 'a'), true) ||
        validTextRequest("", "text", true) ||
        validTextRequest(std::string(33, 'a'), "text", true) ||
        validTextRequest("bob", "", true) ||
        validTextRequest("bob", "text", false)) {
        std::cerr << "Invalid text request validation\n";
        return 1;
    }
    if (!checkSend(false, true, SaveFailed, "S") ||
        !checkSend(true, false, SavedForRetry, "ST") ||
        !checkSend(true, true, TransportQueued, "ST")) {
        std::cerr << "Invalid outgoing message order\n";
        return 1;
    }
    if (!checkReceive(ReceiveFailed, "S") || !checkReceive(NonText, "S") ||
        !checkReceive(DuplicateText, "SA") || !checkReceive(TextSaved, "SA")) {
        std::cerr << "Invalid incoming message order\n";
        return 1;
    }
    return 0;
}
