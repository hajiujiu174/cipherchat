#pragma once
#include "common/protocol/messages.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ccfw {
enum class MessageStage { LocalQueued, WaitingIdentity, EncryptedPersisted, TransportQueued, ServerAccepted, RecipientReceived, Failed };
struct MessageStageEvent {
    std::string messageId;
    MessageStage stage = MessageStage::LocalQueued;
    std::int64_t observedAt = 0; // Local wall clock; 0 means unavailable after restart.
    std::int64_t elapsedMs = -1; // Local steady clock only; never a remote/reading time.
    unsigned retryCount = 0;
    int errorCode = -1;
};
struct ConnectionSecurity { bool tlsValidated = false; std::string serverScope; };
struct WorkspaceState {
    std::string selectedPeer;
    std::int64_t selectedGroup = 0;
    std::vector<std::string> pinnedPeers;
    std::map<std::string, std::string> drafts;
};
enum class BundleState { Preparing, Transferring, Complete, Cancelled, Confirmed };
struct DeliveryItem {
    std::string id, name, sha256, transferId;
    std::uint64_t size = 0;
    std::uint64_t authenticatedBytes = 0;
    bool complete = false, cancelled = false;
    std::string error;
};
struct DeliveryBundle {
    std::string id, sender, recipient, title, description, previousId, manifestDigest;
    std::int64_t createdAt = 0;
    std::vector<DeliveryItem> items;
    BundleState state = BundleState::Preparing;
    bool announced = false, manuallyConfirmed = false;
    std::string confirmationMessageId;
};
struct FeatureState {
    WorkspaceState workspace;
    std::vector<DeliveryBundle> bundles;
    std::map<std::string, nlohmann::json> messageExtensions;
    std::map<std::string, std::filesystem::path> deliverySources; // Local only, DPAPI protected.
};
struct CreateDeliveryBundle {
    std::string operationId, recipient, title, description, previousId;
    std::vector<std::filesystem::path> files;
};
struct ResumeDeliveryItem { std::string bundleId, itemId; };
struct CancelDeliveryBundle { std::string bundleId; };
struct ConfirmDelivery { std::string bundleId, manifestDigest; };
}
