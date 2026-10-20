#include "rhine_json.hpp"
#include "../src/snapshot_codec.hpp"
#include "../src/feature_codec.hpp"
#include "common/base64.hpp"
#include <type_traits>

namespace ccfw::gui::rhine {
namespace {
using Json = nlohmann::json;
Json identity(const PeerIdentity& p) {
    if (p.username.empty()) return Json::object();
    const auto hash = cc::crypto::sha256(p.signingPublicKey + p.dhPublicKey);
    const char* digits = "0123456789ABCDEF"; std::string fingerprint;
    for (std::size_t i = 0; i < hash.size(); ++i) {
        if (i && i % 4 == 0) fingerprint += ' ';
        const auto byte = static_cast<unsigned char>(hash[i]);
        fingerprint += digits[byte >> 4]; fingerprint += digits[byte & 15];
    }
    return {{"username", p.username}, {"signing", cc::base64Encode(p.signingPublicKey)},
        {"dh", cc::base64Encode(p.dhPublicKey)}, {"verified", p.manuallyVerified}, {"fingerprint", fingerprint}};
}
Json file(const FileProgress& p) {
    return {{"fileId", p.fileId}, {"authenticatedBytes", p.authenticatedBytes}, {"totalBytes", p.totalBytes},
        {"complete", p.complete}, {"peer", p.peer}, {"name", p.name}, {"path", codec::pathText(p.path)},
        {"outgoing", p.outgoing}, {"cancelled", p.cancelled}, {"transportPath", p.transportPath}, {"timestamp", p.timestamp},{"cacheUploaded",p.cacheUploaded}};
}
Json chatRecord(const ChatRecord& h) { Json result(h); result["extensions"] = h.extensions; return result; }
}
Json eventJson(const WorkerEvent& item) {
    if (const auto* code = std::get_if<RegistrationCodeEvent>(&item))
        return {{"type", code->passwordResetResult ? "passwordResetResult" : "registrationCode"}, {"ok", code->ok}, {"email", code->email}, {"error", code->error}, {"retryAfter", code->retryAfter}};
    if (const auto* restored = std::get_if<RestoredView>(&item)) {
        Json files = Json::array(); for (const auto& p : restored->files) files.push_back(file(p));
        Json history = Json::array(); for (const auto& h : restored->history) history.push_back(chatRecord(h));
        Json peerIdentities = Json::array(); for (const auto& peer : restored->peerIdentities) peerIdentities.push_back(identity(peer));
        return {{"type", "restored"}, {"username", restored->username}, {"identity", identity(restored->ownIdentity)},
            {"history", history}, {"files", files}, {"groups", restored->groups}, {"scope", restored->scope},
            {"workspace", restored->workspace}, {"bundles", restored->bundles}, {"stages", restored->stages}, {"peerIdentities", peerIdentities}};
    }
    return std::visit([](const auto& value) -> Json {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, AccountEvent>) return {{"type", "account"}, {"phase", static_cast<int>(value.phase)}};
        else if constexpr (std::is_same_v<T, ChatRecord>) return {{"type", "message"}, {"value", chatRecord(value)}};
        else if constexpr (std::is_same_v<T, MessageStageEvent>) return {{"type", "messageStage"}, {"value", Json(value)}};
        else if constexpr (std::is_same_v<T, ConnectionSecurity>) return {{"type", "connectionSecurity"}, {"tlsValidated", value.tlsValidated}, {"serverScope", value.serverScope}};
        else if constexpr (std::is_same_v<T, WorkspaceState>) return {{"type", "workspace"}, {"value", Json(value)}};
        else if constexpr (std::is_same_v<T, DeliveryBundle>) return {{"type", "deliveryBundle"}, {"value", Json(value)}};
        else if constexpr (std::is_same_v<T, FileProgress>) return {{"type", "file"}, {"value", file(value)}};
        else if constexpr (std::is_same_v<T, PeerIdentity>) return {{"type", "identity"}, {"value", identity(value)}};
        else if constexpr (std::is_same_v<T, GroupSnapshot>) return {{"type", "group"}, {"value", Json(value)}};
        else if constexpr (std::is_same_v<T, GroupSendReport>) return {{"type", "groupReport"}, {"value", Json(value)}};
        else if constexpr (std::is_same_v<T, PresenceEvent>) return {{"type", "presence"}, {"username", value.username}, {"online", value.online}};
        else if constexpr (std::is_same_v<T, TypingEvent>) return {{"type", "typing"}, {"username", value.username}, {"active", value.active}};
        else if constexpr (std::is_same_v<T, ContactListEvent>) return {{"type", "contacts"}, {"users", value.users}, {"incoming", value.incoming}, {"outgoing", value.outgoing}};
        else if constexpr (std::is_same_v<T, StrangerListEvent>) return {{"type", "strangers"}, {"users", value.users}, {"query", value.query}, {"next", value.next}};
        else if constexpr (std::is_same_v<T, UserListEvent>) return {{"type", "users"}, {"users", value.users}};
        else if constexpr (std::is_same_v<T, FriendResultEvent>) return {{"type", "friendResult"}, {"ok", value.ok}, {"username", value.username}, {"error", value.error}, {"state", value.state}};
        else if constexpr (std::is_same_v<T, SocialEvent>) return {{"type","social"},{"value",value.value}};
        else if constexpr (std::is_same_v<T, UserUidEvent>) return {{"type", "uid"}, {"username", value.username}, {"uid", value.uid}};
        else return {{"type", "error"}, {"code", static_cast<int>(value.code)}, {"message", value.message}};
    }, std::get<ClientEvent>(item));
}
Command commandFromJson(const Json& j) {
    const auto name = j.at("command").get<std::string>();
    auto text = [&](const char* key) { return j.at(key).get<std::string>(); };
    if (name == "authenticate") {
        const auto p = j.at("port").get<int>();
        if (p < 1 || p > 65535) throw std::runtime_error("invalid port");
        Authenticate request;
        request.connection = {text("host"), static_cast<std::uint16_t>(p), codec::path(text("caFile")), text("serverName")};
        request.dataDirectory = codec::path(text("dataDirectory"));
        request.username = text("username"); request.password = text("password");
        request.registerNew = j.value("registerNew", false); request.directEnabled = j.value("directEnabled", true);
        request.accountServerId = j.value("accountScope", "");
        request.email = j.value("email", ""); request.emailCode = j.value("emailCode", "");
        request.requestCodeOnly = j.value("requestCodeOnly", false);
        request.passwordReset = j.value("passwordReset",false);
        request.emailLogin = j.value("emailLogin",false);
        return request;
    }
    if (name == "social") { const auto& value=j.at("value");if(!value.is_object()||value.dump().size()>420*1024)throw std::runtime_error("invalid social request");return SocialRequest{value}; }
    if (name == "sendText") return SendText{text("peer"), text("text"), 0, {}};
    if (name == "sendFile") return SendFile{text("peer"), codec::path(text("path"))};
    if (name == "workspace") return SetWorkspace{j.at("value").get<WorkspaceState>()};
    if (name == "createDelivery") {
        CreateDeliveryBundle request{ text("operationId"), text("peer"), text("title"), j.value("description", ""), j.value("previousId", ""), {} };
        for (const auto& p : j.at("files")) request.files.push_back(codec::path(p.get<std::string>())); return request;
    }
    if (name == "resumeDeliveryItem") return ResumeDeliveryItem{text("bundleId"), text("itemId")};
    if (name == "cancelDelivery") return CancelDeliveryBundle{text("bundleId")};
    if (name == "confirmDelivery") return ConfirmDelivery{text("bundleId"), text("digest")};
    if (name == "inspectIdentity") return InspectIdentity{text("peer")};
    if (name == "confirmIdentity") {
        PeerIdentity p; p.username = text("peer");
        if (!cc::base64Decode(text("signing"), p.signingPublicKey) || !cc::base64Decode(text("dh"), p.dhPublicKey)
            || p.signingPublicKey.size() != 32 || p.dhPublicKey.size() != 32) throw std::runtime_error("invalid identity");
        return ConfirmIdentity{p};
    }
    if (name == "createGroup") return CreateGroup{text("name"), j.at("members").get<std::vector<std::string>>()};
    if (name == "sendGroup") return SendGroup{j.at("groupId").get<std::int64_t>(), text("text")};
    if (name == "retryGroup") return RetryGroup{text("logicalId")};
    if (name == "typing") return TypeTo{text("peer")};
    if (name == "resumeFile") return ResumeFile{text("fileId")};
    if (name == "cancelFile") return CancelFile{text("fileId")};
    if (name == "directEnabled") return SetDirectEnabled{j.at("enabled").get<bool>()};
    if (name == "searchUsers") return SearchUsers{j.value("query", ""), j.value("after", "")};
    if (name == "addFriend") return AddFriend{text("username")};
    if (name == "respondFriend") return RespondFriend{text("username"), j.at("accept").get<bool>()};
    if (name == "disconnect") return Disconnect{};
    throw std::runtime_error("unknown command");
}
}
