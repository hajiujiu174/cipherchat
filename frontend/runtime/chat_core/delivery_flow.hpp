// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_DELIVERY_FLOW_HPP
#define NEXTALK_CHAT_DELIVERY_FLOW_HPP

#include "file_flow.hpp"

namespace ccfw {

struct BundleState {
    enum { Preparing, Transferring, Complete, Cancelled, Confirmed };
};

struct DeliveryItem {
    std::string id;
    std::string name;
    std::string sha256;
    std::string transferId;
    uint64_t size;
    uint64_t authenticatedBytes;
    bool complete;
    bool cancelled;
    std::string error;

    DeliveryItem();
};

struct DeliveryBundle {
    std::string id;
    std::string sender;
    std::string recipient;
    std::string title;
    std::string description;
    std::string previousId;
    std::string manifestDigest;
    int64_t createdAt;
    std::vector<DeliveryItem> items;
    int state;
    bool announced;
    bool manuallyConfirmed;
    std::string confirmationMessageId;

    DeliveryBundle();
};

double deliveryProgress(const DeliveryBundle& bundle);

} // namespace ccfw

namespace nextalk {
namespace chat {

bool canCreateDelivery(const std::string& account, const std::string& id,
                       const std::string& recipient, const std::string& title,
                       const std::string& description, std::size_t items, std::size_t bundles);
bool matchesDeliveryRequest(const ccfw::DeliveryBundle& bundle, const std::string& sender,
                            const std::string& recipient, const std::string& title,
                            const std::string& description, const std::string& previous,
                            std::size_t items);
bool validManifest(const ccfw::DeliveryBundle& bundle);
bool validStoredDelivery(const ccfw::DeliveryBundle& bundle);
bool canSendDelivery(const ccfw::DeliveryBundle& bundle, const std::string& user);
bool permitsDeliveryFile(const ccfw::DeliveryBundle& bundle, const std::string& sender,
                         const std::string& recipient, const std::string& itemId,
                         const std::string& fileId);
bool shouldStartDeliveryFile(const ccfw::DeliveryItem& item, bool hasTransfer);
void updateDeliveryFile(const ccfw::DeliveryBundle& bundle, const std::string& user,
                        ccfw::DeliveryItem& item, const FileState& transfer);
bool canAnnounceDelivery(const ccfw::DeliveryBundle& bundle);
void updateDeliveryState(ccfw::DeliveryBundle& bundle);
bool canConfirmDelivery(const ccfw::DeliveryBundle& bundle, const std::string& receiver,
                        const std::string& digest);
bool completeDeliveryFile(const ccfw::DeliveryBundle& bundle, const ccfw::DeliveryItem& item,
                          const FileState& transfer);

} // namespace chat
} // namespace nextalk

#endif
