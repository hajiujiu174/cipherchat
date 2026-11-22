#include "ccfw/server_services.hpp"
#include "common/crypto/crypto.hpp"
#include "relay_store.hpp"

#include <algorithm>
#include <chrono>

namespace ccfw::server {
namespace {
namespace chat = nextalk::chat;

Error queueError(chat::QueueStatus status) {
    auto code = ErrorCode::StorageFailure;
    switch (status) {
    case chat::QueueClosed: code = ErrorCode::InvalidState; break;
    case chat::QueueInvalid: code = ErrorCode::InvalidArgument; break;
    case chat::QueueConflict: code = ErrorCode::Conflict; break;
    case chat::QueueFull: code = ErrorCode::Forbidden; break;
    case chat::QueueAlreadyProcessed: code = ErrorCode::AlreadyProcessed; break;
    default: break;
    }
    return {code, "M2-03", "可靠投递队列操作失败"};
}

Result<> queueResult(chat::QueueStatus status) {
    return status == chat::QueueOk ? Result<>::success({}) : Result<>::failure(queueError(status));
}

std::string digest(const std::string& payload) {
    return cc::crypto::sha256(payload);
}

PendingDelivery delivery(chat::PendingMessage message) {
    return {std::move(message.recipient), std::move(message.sender),
            std::move(message.messageId), std::move(message.payload)};
}
}

struct SqliteDeliveryQueue::Impl { chat::RelayStore store; };

SqliteDeliveryQueue::SqliteDeliveryQueue() = default;
SqliteDeliveryQueue::~SqliteDeliveryQueue() = default;

Result<> SqliteDeliveryQueue::open(const std::string& path) {
    try {
        auto candidate = std::make_unique<Impl>();
        const auto status = candidate->store.open(path);
        if (status == chat::QueueOk) impl_ = std::move(candidate);
        return queueResult(status);
    } catch (const std::exception&) {
        return queueResult(chat::QueueStorageError);
    }
}

Result<std::optional<PendingDelivery>> SqliteDeliveryQueue::find(
    const std::string& recipient, const std::string& id) {
    using Response = Result<std::optional<PendingDelivery>>;
    if (!impl_) return Response::failure(queueError(chat::QueueClosed));
    bool found = false;
    chat::PendingMessage message;
    const auto status = impl_->store.find(recipient, id, found, message);
    if (status != chat::QueueOk) return Response::failure(queueError(status));
    return found ? Response::success(delivery(std::move(message))) : Response::success(std::nullopt);
}

Result<> SqliteDeliveryQueue::enqueue(const PendingDelivery& message) {
    if (!impl_) return queueResult(chat::QueueClosed);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return queueResult(impl_->store.enqueue(
        {message.recipient, message.sender, message.messageId, message.ciphertextJson}, now, digest));
}

Result<std::vector<PendingDelivery>> SqliteDeliveryQueue::peek(const std::string& recipient, std::size_t limit) {
    using Response = Result<std::vector<PendingDelivery>>;
    if (!impl_) return Response::failure(queueError(chat::QueueClosed));
    std::vector<chat::PendingMessage> pending;
    const auto status = impl_->store.peek(recipient, limit, pending);
    if (status != chat::QueueOk) return Response::failure(queueError(status));
    std::vector<PendingDelivery> messages;
    messages.reserve(pending.size());
    for (auto& message : pending) messages.push_back(delivery(std::move(message)));
    return Response::success(std::move(messages));
}

Result<> SqliteDeliveryQueue::acknowledge(const std::string& recipient, const std::string& id) {
    return queueResult(impl_ ? impl_->store.acknowledge(recipient, id, digest) : chat::QueueClosed);
}

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

