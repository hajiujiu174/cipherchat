// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_RELAY_STORE_HPP
#define NEXTALK_CHAT_RELAY_STORE_HPP

#include "sqlite_core.hpp"
#include <vector>

namespace nextalk {
namespace chat {

enum QueueStatus {
    QueueOk, QueueClosed, QueueInvalid, QueueConflict,
    QueueFull, QueueAlreadyProcessed, QueueStorageError
};

struct PendingMessage {
    std::string recipient;
    std::string sender;
    std::string messageId;
    std::string payload;
    PendingMessage() {}
    PendingMessage(const std::string& to, const std::string& from,
                   const std::string& id, const std::string& bytes)
        : recipient(to), sender(from), messageId(id), payload(bytes) {}
};

typedef std::string (*DigestFunction)(const std::string&);

class RelayStore {
public:
    QueueStatus open(const std::string& path);
    QueueStatus find(const std::string& recipient, const std::string& id,
                     bool& found, PendingMessage& message);
    QueueStatus enqueue(const PendingMessage& message, sqlite3_int64 createdAt,
                        DigestFunction digest);
    QueueStatus peek(const std::string& recipient, std::size_t limit,
                     std::vector<PendingMessage>& messages);
    QueueStatus acknowledge(const std::string& recipient, const std::string& id,
                            DigestFunction digest);

private:
    ccfw::sql::Database database_;
};

} // namespace chat
} // namespace nextalk

#endif
