// 人工实现，由 ChatGPT 规范化。

#include "relay_store.hpp"

#include <stdexcept>

namespace nextalk {
namespace chat {
namespace {
using ccfw::sql::Statement;
using ccfw::sql::Transaction;

const int kMaxPeerMessages = 4096;
const int kMaxMessageBytes = 8 * 1024 * 1024;
const int kMaxPeerBytes = 64 * 1024 * 1024;
const char kSchema[] =
    "CREATE TABLE IF NOT EXISTS pending_deliveries_v2 (\n"
    " recipient TEXT NOT NULL, message_id TEXT NOT NULL, sender TEXT NOT NULL,\n"
    " ciphertext_json TEXT NOT NULL, created_at_ms INTEGER NOT NULL,\n"
    " PRIMARY KEY(recipient, message_id));\n"
    "CREATE INDEX IF NOT EXISTS pending_deliveries_v2_order\n"
    " ON pending_deliveries_v2(recipient, created_at_ms, message_id);\n"
    "CREATE TABLE IF NOT EXISTS delivery_receipts_v2 (\n"
    " recipient TEXT NOT NULL, message_id TEXT NOT NULL, sender TEXT NOT NULL,\n"
    " ciphertext_sha256 BLOB NOT NULL, PRIMARY KEY(recipient, message_id));\n";

bool valid(const PendingMessage& message) {
    return !message.recipient.empty() && message.recipient.size() <= 32 &&
           !message.sender.empty() && message.sender.size() <= 32 &&
           !message.messageId.empty() && message.messageId.size() <= 128 &&
           !message.payload.empty() && message.payload.size() <= kMaxMessageBytes;
}
}

// 使用原有的表和索引，升级时直接复用已有数据库文件。
QueueStatus RelayStore::open(const std::string& path) {
    try {
        ccfw::sql::Database candidate;
        candidate.open(path);
        ccfw::sql::exec(candidate.value, kSchema);
        database_.swap(candidate);
        return QueueOk;
    } catch (const std::exception&) {
        return QueueStorageError;
    }
}

QueueStatus RelayStore::find(const std::string& recipient, const std::string& id,
                             bool& found, PendingMessage& message) {
    found = false;
    if (!database_.value) return QueueClosed;
    try {
        Statement query(database_.value,
            "SELECT sender,ciphertext_json FROM pending_deliveries_v2 WHERE recipient=?1 AND message_id=?2");
        query.text(1, recipient);
        query.text(2, id);
        if (query.step()) {
            message = PendingMessage(recipient, query.bytes(0), id, query.bytes(1));
            found = true;
        }
        return QueueOk;
    } catch (const std::exception&) {
        return QueueStorageError;
    }
}

// 优先核对待投递项和签收记录，再检查容量和插入新任务。
QueueStatus RelayStore::enqueue(const PendingMessage& message, sqlite3_int64 createdAt,
                                DigestFunction digest) {
    sqlite3* db = database_.value;
    if (!db) return QueueClosed;
    if (!valid(message)) return QueueInvalid;
    try {
        Transaction transaction(db);
        PendingMessage prior;
        bool found = false;
        const QueueStatus lookup = find(message.recipient, message.messageId, found, prior);
        if (lookup != QueueOk) return lookup;
        if (found) {
            const bool same = prior.sender == message.sender && prior.payload == message.payload;
            return same ? QueueOk : QueueConflict;
        }
        Statement receipt(db,
            "SELECT sender,ciphertext_sha256 FROM delivery_receipts_v2 WHERE recipient=?1 AND message_id=?2");
        receipt.text(1, message.recipient);
        receipt.text(2, message.messageId);
        if (receipt.step()) {
            const bool same = receipt.bytes(0) == message.sender &&
                              receipt.bytes(1) == digest(message.payload);
            return same ? QueueAlreadyProcessed : QueueConflict;
        }
        Statement capacity(db,
            "SELECT count(*),coalesce(sum(length(ciphertext_json)),0) FROM pending_deliveries_v2 WHERE recipient=?1");
        capacity.text(1, message.recipient);
        if (!capacity.step() || capacity.number(0) >= kMaxPeerMessages ||
            capacity.number(1) + static_cast<sqlite3_int64>(message.payload.size()) > kMaxPeerBytes)
            return QueueFull;

        Statement insert(db, "INSERT INTO pending_deliveries_v2 VALUES(?1,?2,?3,?4,?5)");
        insert.text(1, message.recipient);
        insert.text(2, message.messageId);
        insert.text(3, message.sender);
        insert.text(4, message.payload);
        insert.number(5, createdAt);
        insert.step();
        transaction.commit();
        return QueueOk;
    } catch (const std::exception&) {
        return QueueStorageError;
    }
}

// 按数据库里的入队顺序读取一批消息，等签收后再删除。
QueueStatus RelayStore::peek(const std::string& recipient, std::size_t limit,
                             std::vector<PendingMessage>& messages) {
    if (!database_.value) return QueueClosed;
    if (limit > kMaxPeerMessages) return QueueInvalid;
    try {
        Statement query(database_.value,
            "SELECT sender,message_id,ciphertext_json FROM pending_deliveries_v2 WHERE recipient=?1 ORDER BY rowid LIMIT ?2");
        query.text(1, recipient);
        query.number(2, static_cast<sqlite3_int64>(limit));
        std::vector<PendingMessage> result;
        std::size_t bytes = 0;
        while (query.step()) {
            const std::string payload = query.bytes(2);
            if (bytes + payload.size() > kMaxMessageBytes) break;
            bytes += payload.size();
            result.push_back(PendingMessage(recipient, query.bytes(0), query.bytes(1), payload));
        }
        messages.swap(result);
        return QueueOk;
    } catch (const std::exception&) {
        return QueueStorageError;
    }
}

// 记录签收和删除待投递项必须在同一个数据库事务内完成。
QueueStatus RelayStore::acknowledge(const std::string& recipient, const std::string& id,
                                    DigestFunction digest) {
    sqlite3* db = database_.value;
    if (!db) return QueueClosed;
    try {
        Transaction transaction(db);
        PendingMessage message;
        bool found = false;
        const QueueStatus lookup = find(recipient, id, found, message);
        if (lookup != QueueOk || !found) return lookup;
        Statement receipt(db, "INSERT INTO delivery_receipts_v2 VALUES(?1,?2,?3,?4)");
        receipt.text(1, recipient);
        receipt.text(2, id);
        receipt.text(3, message.sender);
        receipt.blob(4, digest(message.payload));
        receipt.step();
        Statement remove(db, "DELETE FROM pending_deliveries_v2 WHERE recipient=?1 AND message_id=?2");
        remove.text(1, recipient);
        remove.text(2, id);
        remove.step();
        transaction.commit();
        return QueueOk;
    } catch (const std::exception&) {
        return QueueStorageError;
    }
}

} // namespace chat
} // namespace nextalk
