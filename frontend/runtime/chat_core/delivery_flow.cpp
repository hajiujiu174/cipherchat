// 人工实现，由 ChatGPT 规范化。

#include "delivery_flow.hpp"
#include "binary.hpp"
#include <algorithm>
#include <set>

namespace ccfw {

DeliveryItem::DeliveryItem() : size(0), authenticatedBytes(0), complete(false), cancelled(false) {}

DeliveryBundle::DeliveryBundle()
    : createdAt(0), state(BundleState::Preparing), announced(false), manuallyConfirmed(false) {}

double deliveryProgress(const DeliveryBundle& bundle) {
    uint64_t bytes = 0;
    uint64_t total = 0;
    unsigned completed = 0;
    for (std::vector<DeliveryItem>::const_iterator it = bundle.items.begin(); it != bundle.items.end(); ++it) {
        bytes += std::min(it->size, it->authenticatedBytes);
        total += it->size;
        if (it->complete) ++completed;
    }
    if (total != 0) return static_cast<double>(bytes) / static_cast<double>(total);
    if (bundle.items.empty()) return 0;
    return static_cast<double>(completed) / bundle.items.size();
}

} // namespace ccfw

namespace nextalk {
namespace chat {
namespace {

bool validTitle(const std::string& title, const std::string& description) {
    return !title.empty() && title.size() <= 256 && description.size() <= 4096;
}

bool validItemCount(std::size_t items) {
    return items > 0 && items <= 16;
}

bool decodeFixed(const std::string& text, std::size_t length) {
    std::string decoded;
    return base64Decode(text.data(), text.size(), decoded) && decoded.size() == length;
}

} // namespace

bool canCreateDelivery(const std::string& account, const std::string& id,
                       const std::string& recipient, const std::string& title,
                       const std::string& description, std::size_t items, std::size_t bundles) {
    return !account.empty() && !id.empty() && id.size() <= 64 &&
        !recipient.empty() && recipient.size() <= 32 && recipient != account &&
        validTitle(title, description) && validItemCount(items) && bundles < 2048;
}

bool matchesDeliveryRequest(const ccfw::DeliveryBundle& bundle, const std::string& sender,
                            const std::string& recipient, const std::string& title,
                            const std::string& description, const std::string& previous,
                            std::size_t items) {
    return bundle.sender == sender && bundle.recipient == recipient &&
        bundle.title == title && bundle.description == description &&
        bundle.previousId == previous && bundle.items.size() == items;
}

// 清单和文件传输各自有一个 ID，两种 ID 都必须在清单中唯一。
bool validManifest(const ccfw::DeliveryBundle& bundle) {
    if (bundle.id.empty() || bundle.id.size() > 64 || bundle.id == bundle.previousId ||
        bundle.previousId.size() > 64 || bundle.sender.empty() || bundle.sender.size() > 32 ||
        bundle.recipient.empty() || bundle.recipient.size() > 32 || bundle.sender == bundle.recipient ||
        !validTitle(bundle.title, bundle.description) || !validItemCount(bundle.items.size())) {
        return false;
    }
    std::set<std::string> ids;
    std::set<std::string> transfers;
    for (std::vector<ccfw::DeliveryItem>::const_iterator it = bundle.items.begin(); it != bundle.items.end(); ++it) {
        if (it->id.empty() || it->id.size() > 64 || !ids.insert(it->id).second ||
            it->name.empty() || it->name.size() > 200 || it->size > kMaxFileBytes ||
            it->sha256.size() != 44 || !decodeFixed(it->sha256, 32) ||
            it->transferId.size() > 32 || !decodeFixed(it->transferId, 16) ||
            !transfers.insert(it->transferId).second) {
            return false;
        }
    }
    return true;
}

bool validStoredDelivery(const ccfw::DeliveryBundle& bundle) {
    return !bundle.id.empty() && bundle.id.size() <= 64 && validItemCount(bundle.items.size()) &&
        bundle.state >= ccfw::BundleState::Preparing && bundle.state <= ccfw::BundleState::Confirmed;
}

bool canSendDelivery(const ccfw::DeliveryBundle& bundle, const std::string& user) {
    return bundle.sender == user && bundle.state != ccfw::BundleState::Cancelled;
}

bool permitsDeliveryFile(const ccfw::DeliveryBundle& bundle, const std::string& sender,
                         const std::string& recipient, const std::string& itemId,
                         const std::string& fileId) {
    if (!canSendDelivery(bundle, sender) || bundle.recipient != recipient) return false;
    for (std::vector<ccfw::DeliveryItem>::const_iterator it = bundle.items.begin(); it != bundle.items.end(); ++it) {
        if (it->id == itemId && it->transferId == fileId) return true;
    }
    return false;
}

bool shouldStartDeliveryFile(const ccfw::DeliveryItem& item, bool hasTransfer) {
    return item.error.empty() && !hasTransfer;
}

void updateDeliveryFile(const ccfw::DeliveryBundle& bundle, const std::string& user,
                        ccfw::DeliveryItem& item, const FileState& transfer) {
    const bool sending = bundle.sender == user;
    const std::string& peer = sending ? bundle.recipient : bundle.sender;
    if (transfer.peer != peer || transfer.outgoing != sending || transfer.totalBytes != item.size) {
        item.error = "文件与清单不匹配";
        return;
    }
    if (!transfer.sourceDigest.empty()) {
        if (bundle.announced && transfer.sourceDigest != item.sha256) {
            item.error = "文件摘要与清单不匹配";
            return;
        }
        if (!bundle.announced) item.sha256 = transfer.sourceDigest;
    }
    item.authenticatedBytes = std::min(transfer.totalBytes,
        static_cast<uint64_t>(transfer.nextContiguousChunk) * kFileChunkSize);
    item.complete = transfer.complete;
    item.cancelled = transfer.cancelled;
}

bool canAnnounceDelivery(const ccfw::DeliveryBundle& bundle) {
    if (bundle.announced || bundle.state == ccfw::BundleState::Cancelled) return false;
    for (std::vector<ccfw::DeliveryItem>::const_iterator it = bundle.items.begin(); it != bundle.items.end(); ++it) {
        if (it->sha256.empty() || !it->error.empty()) return false;
    }
    return true;
}

void updateDeliveryState(ccfw::DeliveryBundle& bundle) {
    if (bundle.state == ccfw::BundleState::Cancelled) return;
    if (bundle.manuallyConfirmed) {
        bundle.state = ccfw::BundleState::Confirmed;
        return;
    }
    for (std::vector<ccfw::DeliveryItem>::const_iterator it = bundle.items.begin(); it != bundle.items.end(); ++it) {
        if (!it->complete || !it->error.empty()) {
            bundle.state = bundle.announced ? ccfw::BundleState::Transferring : ccfw::BundleState::Preparing;
            return;
        }
    }
    bundle.state = ccfw::BundleState::Complete;
}

bool canConfirmDelivery(const ccfw::DeliveryBundle& bundle, const std::string& receiver,
                        const std::string& digest) {
    return bundle.announced && bundle.recipient == receiver && bundle.manifestDigest == digest;
}

bool completeDeliveryFile(const ccfw::DeliveryBundle& bundle, const ccfw::DeliveryItem& item,
                          const FileState& transfer) {
    return !transfer.outgoing && transfer.peer == bundle.sender && transfer.complete &&
        !transfer.cancelled && transfer.totalBytes == item.size && transfer.sourceDigest == item.sha256;
}

} // namespace chat
} // namespace nextalk
