#include "ccfw/server_services.hpp"
#include "sqlite_helpers.hpp"
#include <chrono>
#include <algorithm>
namespace ccfw::server {
// ChallengeBook（M2-02）以内联形式定义于头文件，保持与原项目 router 的单一来源共享。
namespace {
Error queueError(ErrorCode code = ErrorCode::StorageFailure) {
    return {code, "M2-03", "可靠投递队列操作失败"};
}
constexpr auto schema = R"(
CREATE TABLE IF NOT EXISTS pending_deliveries_v2 (
 recipient TEXT NOT NULL, message_id TEXT NOT NULL, sender TEXT NOT NULL,
 ciphertext_json TEXT NOT NULL, created_at_ms INTEGER NOT NULL,
 PRIMARY KEY(recipient, message_id));
CREATE INDEX IF NOT EXISTS pending_deliveries_v2_order
 ON pending_deliveries_v2(recipient, created_at_ms, message_id);
CREATE TABLE IF NOT EXISTS delivery_receipts_v2 (
 recipient TEXT NOT NULL, message_id TEXT NOT NULL, sender TEXT NOT NULL,
 ciphertext_sha256 BLOB NOT NULL, PRIMARY KEY(recipient, message_id));
)";
}
struct SqliteDeliveryQueue::Impl { sql::Database database; };
SqliteDeliveryQueue::SqliteDeliveryQueue() = default;
SqliteDeliveryQueue::~SqliteDeliveryQueue() = default;
Result<> SqliteDeliveryQueue::open(const std::string& path) {
    try {
        auto candidate = std::make_unique<Impl>();
        candidate->database.open(path);
        sql::exec(candidate->database.value, schema);
        impl_ = std::move(candidate);
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(queueError()); }
}
Result<std::optional<PendingDelivery>> SqliteDeliveryQueue::find(
    const std::string& recipient, const std::string& id) {
    if (!impl_) return Result<std::optional<PendingDelivery>>::failure(queueError(ErrorCode::InvalidState));
    try {
        sql::Statement query(impl_->database.value,
            "SELECT sender,ciphertext_json FROM pending_deliveries_v2 WHERE recipient=?1 AND message_id=?2");
        query.text(1, recipient); query.text(2, id);
        if (!query.step()) return Result<std::optional<PendingDelivery>>::success(std::nullopt);
        return Result<std::optional<PendingDelivery>>::success(PendingDelivery{recipient, query.bytes(0), id, query.bytes(1)});
    } catch (const std::exception&) { return Result<std::optional<PendingDelivery>>::failure(queueError()); }
}
Result<> SqliteDeliveryQueue::enqueue(const PendingDelivery& message) {
    if (!impl_) return Result<>::failure(queueError(ErrorCode::InvalidState));
    if (message.recipient.empty() || message.sender.empty() || message.messageId.empty() ||
        message.recipient.size() > 32 || message.sender.size() > 32 ||
        message.messageId.size() > 128 || message.ciphertextJson.empty() ||
        message.ciphertextJson.size() > 8 * 1024 * 1024)
        return Result<>::failure(queueError(ErrorCode::InvalidArgument));
    try {
        const auto db = impl_->database.value;
        sql::Transaction transaction(db);
        auto prior = find(message.recipient, message.messageId);
        if (!prior) return Result<>::failure(prior.error());
        if (prior.value()) {
            if (prior.value()->sender != message.sender || prior.value()->ciphertextJson != message.ciphertextJson)
                return Result<>::failure(queueError(ErrorCode::Conflict));
            return Result<>::success({});
        }
        sql::Statement receipt(db, "SELECT sender,ciphertext_sha256 FROM delivery_receipts_v2 WHERE recipient=?1 AND message_id=?2");
        receipt.text(1, message.recipient); receipt.text(2, message.messageId);
        if (receipt.step()) {
            const bool same = receipt.bytes(0) == message.sender &&
                receipt.bytes(1) == cc::crypto::sha256(message.ciphertextJson);
            return Result<>::failure(queueError(same ? ErrorCode::AlreadyProcessed : ErrorCode::Conflict));
        }
        sql::Statement capacity(db, "SELECT count(*),coalesce(sum(length(ciphertext_json)),0) FROM pending_deliveries_v2 WHERE recipient=?1");
        capacity.text(1, message.recipient);
        if (!capacity.step() || capacity.number(0) >= 4096 ||
            capacity.number(1) + static_cast<std::int64_t>(message.ciphertextJson.size()) > 64 * 1024 * 1024)
            return Result<>::failure(queueError(ErrorCode::Forbidden));
        sql::Statement insert(db, "INSERT INTO pending_deliveries_v2 VALUES(?1,?2,?3,?4,?5)");
        insert.text(1, message.recipient); insert.text(2, message.messageId);
        insert.text(3, message.sender); insert.text(4, message.ciphertextJson);
        insert.number(5, std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        insert.step();
        transaction.commit();
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(queueError()); }
}
Result<std::vector<PendingDelivery>> SqliteDeliveryQueue::peek(const std::string& recipient, std::size_t limit) {
    if (!impl_) return Result<std::vector<PendingDelivery>>::failure(queueError(ErrorCode::InvalidState));
    if (limit > 4096) return Result<std::vector<PendingDelivery>>::failure(queueError(ErrorCode::InvalidArgument));
    try {
        sql::Statement query(impl_->database.value,
            "SELECT sender,message_id,ciphertext_json FROM pending_deliveries_v2 WHERE recipient=?1 ORDER BY rowid LIMIT ?2");
        query.text(1, recipient); query.number(2, static_cast<std::int64_t>(limit));
        std::vector<PendingDelivery> result;
        std::size_t bytes = 0;
        while (query.step()) {
            auto ciphertext = query.bytes(2);
            if (bytes + ciphertext.size() > 8 * 1024 * 1024) break;
            bytes += ciphertext.size();
            result.push_back({recipient, query.bytes(0), query.bytes(1), std::move(ciphertext)});
        }
        return Result<std::vector<PendingDelivery>>::success(std::move(result));
    } catch (const std::exception&) { return Result<std::vector<PendingDelivery>>::failure(queueError()); }
}
Result<> SqliteDeliveryQueue::acknowledge(const std::string& recipient, const std::string& id) {
    if (!impl_) return Result<>::failure(queueError(ErrorCode::InvalidState));
    try {
        const auto db = impl_->database.value;
        sql::Transaction transaction(db);
        auto message = find(recipient, id);
        if (!message) return Result<>::failure(message.error());
        if (!message.value()) return Result<>::success({});
        sql::Statement receipt(db, "INSERT INTO delivery_receipts_v2 VALUES(?1,?2,?3,?4)");
        receipt.text(1, recipient); receipt.text(2, id); receipt.text(3, message.value()->sender);
        receipt.blob(4, cc::crypto::sha256(message.value()->ciphertextJson));
        receipt.step();
        sql::Statement remove(db, "DELETE FROM pending_deliveries_v2 WHERE recipient=?1 AND message_id=?2");
        remove.text(1, recipient); remove.text(2, id); remove.step();
        transaction.commit();
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(queueError()); }
}
// 从服务器可信群成员表核对双方资格；外部 group_id 不是授权。
Result<> GroupAuthorizer::authorize(std::int64_t id, const std::string& sender, const std::string& recipient) {
    if (id <= 0 || sender.empty() || recipient.empty() || sender == recipient)
        return Result<>::failure({ErrorCode::Forbidden, "M5-01", "群投递字段非法"});
    auto list = groups_.members(id); if (!list) return Result<>::failure(list.error());
    if (std::find(list.value().begin(), list.value().end(), sender) == list.value().end() ||
        std::find(list.value().begin(), list.value().end(), recipient) == list.value().end())
        return Result<>::failure({ErrorCode::Forbidden, "M5-01", "发件人或收件人不在群中"});
    return Result<>::success({});
}
}

