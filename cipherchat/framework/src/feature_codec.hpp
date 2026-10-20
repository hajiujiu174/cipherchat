#pragma once
#include "snapshot_codec.hpp"
#include <set>
#include <algorithm>

namespace ccfw {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MessageStageEvent, messageId, stage, observedAt, elapsedMs, retryCount, errorCode)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorkspaceState, selectedPeer, selectedGroup, pinnedPeers, drafts)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DeliveryItem, id, name, sha256, transferId, size, authenticatedBytes, complete, cancelled, error)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DeliveryBundle, id, sender, recipient, title, description, previousId, manifestDigest, createdAt, items, state, announced, manuallyConfirmed, confirmationMessageId)
namespace features {
inline void validateWorkspace(const WorkspaceState& state) {
    codec::ensure(state.selectedPeer.size() <= 32 && state.selectedGroup >= 0 && state.pinnedPeers.size() <= 4 && state.drafts.size() <= 64);
    std::set<std::string> peers;
    for (const auto& p : state.pinnedPeers) codec::ensure(!p.empty() && p.size() <= 32 && peers.insert(p).second);
    for (const auto& [key, text] : state.drafts) codec::ensure(key.size() <= 96 && text.size() <= 65536);
}
inline nlohmann::json manifest(const DeliveryBundle& b) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& i : b.items) items.push_back({{"id", i.id}, {"name", i.name}, {"size", i.size}, {"sha256", i.sha256}, {"transferId", i.transferId}});
    return {{"version", 1}, {"id", b.id}, {"sender", b.sender}, {"recipient", b.recipient}, {"title", b.title},
        {"description", b.description}, {"previousId", b.previousId}, {"createdAt", b.createdAt}, {"items", items}};
}
inline std::string digest(const DeliveryBundle& b) { return cc::base64Encode(cc::crypto::sha256(manifest(b).dump())); }
inline DeliveryBundle parseManifest(const nlohmann::json& j) {
    codec::ensure(j.is_object() && j.at("version") == 1 && j.dump().size() <= 32768);
    DeliveryBundle b;
    b.id = j.at("id").get<std::string>(); b.sender = j.at("sender").get<std::string>(); b.recipient = j.at("recipient").get<std::string>();
    b.title = j.at("title").get<std::string>(); b.description = j.at("description").get<std::string>(); b.previousId = j.at("previousId").get<std::string>();
    b.createdAt = static_cast<std::int64_t>(codec::natural(j.at("createdAt")));
    codec::ensure(!b.id.empty() && b.id.size() <= 64 && b.id != b.previousId && b.previousId.size() <= 64 &&
        !b.sender.empty() && b.sender.size() <= 32 && !b.recipient.empty() && b.recipient.size() <= 32 && b.sender != b.recipient &&
        !b.title.empty() && b.title.size() <= 256 && b.description.size() <= 4096 &&
        j.at("items").is_array() && !j.at("items").empty() && j.at("items").size() <= 16);
    std::set<std::string> ids, transfers;
    for (const auto& entry : j.at("items")) {
        DeliveryItem i; i.id = entry.at("id").get<std::string>(); i.name = entry.at("name").get<std::string>();
        i.size = codec::natural(entry.at("size"), 1024ULL * 1024 * 1024);
        i.sha256 = entry.at("sha256").get<std::string>(); i.transferId = entry.at("transferId").get<std::string>(); cc::Bytes bytes;
        codec::ensure(!i.id.empty() && i.id.size() <= 64 && ids.insert(i.id).second && !i.name.empty() && i.name.size() <= 200 &&
            i.sha256.size() == 44 && cc::base64Decode(i.sha256, bytes) && bytes.size() == 32 &&
            i.transferId.size() <= 32 && cc::base64Decode(i.transferId, bytes) && bytes.size() == 16 && transfers.insert(i.transferId).second);
        b.items.push_back(std::move(i));
    }
    b.manifestDigest = digest(b); b.announced = true; b.state = BundleState::Transferring; return b;
}
inline void projectExtension(ClientSnapshot& next, const PlainEnvelope& p) {
    if (p.extensions.is_null() || p.extensions.empty()) return;
    codec::ensure(p.kind == "chat" && p.extensions.is_object() && p.extensions.dump().size() <= 32768);
    next.features.messageExtensions[p.messageId] = p.extensions;
    const auto& ext = p.extensions;
    if (!ext.contains("delivery") || !ext["delivery"].is_object()) return;
    const auto& d = ext["delivery"];
    if (!d.contains("version") || !d["version"].is_number_integer() || d["version"] != 1) return;
    codec::ensure(p.groupId == 0);
    const auto action = d.at("action").get<std::string>();
    if (action == "manifest") {
        auto b = parseManifest(d.at("manifest"));
        codec::ensure(b.sender == p.from && b.recipient == p.to && b.manifestDigest == d.at("digest").get<std::string>());
        auto old = std::find_if(next.features.bundles.begin(), next.features.bundles.end(), [&](const auto& v) { return v.id == b.id; });
        if (old == next.features.bundles.end()) { codec::ensure(next.features.bundles.size() < 2048); next.features.bundles.push_back(std::move(b)); }
        else codec::ensure(old->sender == b.sender && old->recipient == b.recipient && old->manifestDigest == b.manifestDigest);
    } else if (action == "confirm") {
        const auto id = d.at("id").get<std::string>(), hash = d.at("digest").get<std::string>();
        auto b = std::find_if(next.features.bundles.begin(), next.features.bundles.end(), [&](const auto& v) { return v.id == id; });
        codec::ensure(b != next.features.bundles.end() && b->announced && b->recipient == p.from && b->sender == p.to && b->manifestDigest == hash);
        b->manuallyConfirmed = true; b->state = BundleState::Confirmed;
    }
}
inline nlohmann::json encode(const ClientSnapshot& s) {
    nlohmann::json paths = nlohmann::json::object();
    for (const auto& [id, path] : s.features.deliverySources) paths[id] = codec::pathText(path);
    return {{"version", 1}, {"account", s.account}, {"workspace", s.features.workspace}, {"bundles", s.features.bundles},
        {"extensions", s.features.messageExtensions}, {"sources", paths}};
}
inline void decode(const nlohmann::json& j, ClientSnapshot& s) {
    codec::ensure(j.at("version") == 1);
    const auto account = j.at("account").get<AccountScope>(); codec::ensure(account.server == s.account.server && account.username == s.account.username);
    s.features.workspace = j.at("workspace").get<WorkspaceState>(); validateWorkspace(s.features.workspace);
    codec::ensure(j.at("bundles").is_array() && j.at("bundles").size() <= 2048 && j.at("extensions").is_object() && j.at("extensions").size() <= 16384 &&
        j.at("sources").is_object() && j.at("sources").size() <= 32768);
    s.features.bundles = j.at("bundles").get<std::vector<DeliveryBundle>>();
    std::set<std::string> ids;
    for (const auto& b : s.features.bundles) {
        codec::ensure(ids.insert(b.id).second && !b.id.empty() && b.id.size() <= 64 && !b.items.empty() && b.items.size() <= 16 &&
            static_cast<int>(b.state) >= 0 && static_cast<int>(b.state) <= 4);
        if (b.announced) { auto checked = parseManifest(manifest(b)); codec::ensure(checked.manifestDigest == b.manifestDigest); }
    }
    s.features.messageExtensions = j.at("extensions").get<std::map<std::string, nlohmann::json>>();
    for (const auto& [id, ext] : s.features.messageExtensions) codec::ensure(id.size() <= 128 && ext.is_object() && ext.dump().size() <= 32768);
    for (const auto& [id, value] : j.at("sources").items()) { codec::ensure(id.size() <= 64); s.features.deliverySources[id] = codec::path(value.get<std::string>()); }
    for (auto& h : s.history) if (s.features.messageExtensions.count(h.messageId)) h.extensions = s.features.messageExtensions.at(h.messageId);
    for (auto& p : s.pendingSends) if (s.features.messageExtensions.count(p.messageId)) p.extensions = s.features.messageExtensions.at(p.messageId);
}
}
}
