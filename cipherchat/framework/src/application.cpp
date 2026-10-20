#include "ccfw/application.hpp"
#include "wire_helpers.hpp"
namespace ccfw {
ClientApplication::ClientApplication(ITransport& net, AccountService& account, ChatService& chat,
    FileTransferService& files, GroupService& groups, IEventSink& events, Clock clock)
    : net_(net), account_(account), chat_(chat), files_(files), groups_(groups), events_(events), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}
Result<> ClientApplication::listContacts() {
    if (account_.phase() != AccountPhase::Online) return Result<>::failure({ErrorCode::NotConnected, "M9-02", "请先登录"});
    return net_.send(cc::proto::withV2(cc::proto::makeListContacts()));
}
Result<> ClientApplication::social(nlohmann::json request) {
    if(account_.phase()!=AccountPhase::Online)return Result<>::failure({ErrorCode::NotConnected,"M10-34","请先登录"});
    if(!socialSupported_)return Result<>::failure({ErrorCode::NotImplemented,"M10-34","当前服务器暂不支持个人资料和朋友圈"});
    if(!request.is_object()||request.dump().size()>420*1024)return Result<>::failure({ErrorCode::InvalidArgument,"M10-34","个人资料请求过大或格式有误"});
    return net_.send(cc::proto::withV2({cc::proto::MsgType::SocialRequest,"social:"+std::to_string(++searchSequence_),std::move(request)}));
}
Result<> ClientApplication::lookupUid(const std::string& username) {
    if (account_.phase() != AccountPhase::Online) return Result<>::failure({ErrorCode::NotConnected,"M10-19","请先登录"});
    if (!account_.uidLookupSupported()) return Result<>::failure({ErrorCode::NotImplemented,"M10-19","当前服务器尚未启用 UID 查询"});
    if (username.empty() || username.size() > 32) return Result<>::failure({ErrorCode::InvalidArgument,"M10-19","UID 查询账号无效"});
    return net_.send(cc::proto::withV2(cc::proto::makeLookupUid(username)));
}
Result<> ClientApplication::searchUsers(const std::string& query, const std::string& after) {
    if (account_.phase() != AccountPhase::Online) return Result<>::failure({ErrorCode::NotConnected, "M9-02", "请先登录"});
    if (query.size() > 32) return Result<>::failure({ErrorCode::InvalidArgument, "M9-02", "查询内容过长"});
    auto request = cc::proto::withV2(cc::proto::makeSearchUsers(query, after));
    request.msgId = "directory:" + std::to_string(++searchSequence_); searchId_ = request.msgId;
    return net_.send(request);
}
Result<> ClientApplication::addFriend(const std::string& username) {
    if (account_.phase() != AccountPhase::Online) return Result<>::failure({ErrorCode::NotConnected, "M9-02", "请先登录"});
    if (username.empty() || username.size() > 32) return Result<>::failure({ErrorCode::InvalidArgument, "M9-02", "用户名无效"});
    return net_.send(cc::proto::withV2(cc::proto::makeAddFriend(username)));
}
Result<> ClientApplication::respondFriend(const std::string& username, bool accept) {
    if (account_.phase() != AccountPhase::Online) return Result<>::failure({ErrorCode::NotConnected, "M9-02", "请先登录"});
    if (username.empty() || username.size() > 32) return Result<>::failure({ErrorCode::InvalidArgument, "M9-02", "用户名无效"});
    return net_.send(cc::proto::withV2(cc::proto::makeRespondFriend(username, accept)));
}
Result<> ClientApplication::connect(const ConnectionOptions& options) {
    shutdown();
    files_.setCacheSupported(false);
    socialSupported_=false;
    auto connected = net_.connect(options);
    if (!connected) { events_.publish(connected.error()); return connected; }
    events_.publish(ConnectionSecurity{true, options.host + ":" + std::to_string(options.port) + "/" + options.expectedServerName});
    tlsValidated_ = true;
    account_.connectionChanged(true);
    auto negotiated = account_.negotiateProtocol();
    if (!negotiated) { shutdown(); events_.publish(negotiated.error()); return negotiated; }
    events_.publish(AccountEvent{account_.phase()});
    return Result<>::success({});
}
Result<> ClientApplication::tick() {
    using namespace cc::proto;
    auto timeout = account_.checkTimeout();
    if (!timeout) { events_.publish(timeout.error()); events_.publish(AccountEvent{account_.phase()}); return timeout; }
    if (account_.phase() == AccountPhase::Disconnected) return Result<>::success({});
    std::optional<Error> firstError;
    auto publishError = [&](const Error& error) {
        if (error.code == ErrorCode::AlreadyProcessed) return;
        if (!firstError) firstError = error;
        events_.publish(error);
    };
    for (int count = 0; count < 64; ++count) {
        auto incoming = net_.poll();
        if (!incoming) {
            publishError(incoming.error()); shutdown(); events_.publish(AccountEvent{account_.phase()}); break;
        }
        if (!incoming.value()) break;
        const auto& message = *incoming.value();
        const auto before = account_.phase();
        try {
            const bool accountMessage = message.type == MsgType::HelloResult || message.type == MsgType::LoginChallenge ||
                message.type == MsgType::LoginResult || message.type == MsgType::RegisterResult || message.type == MsgType::UploadPrekeysResult || message.type == MsgType::RegistrationCodeResult || message.type == MsgType::PasswordResetResult;
            if (accountMessage || (message.type == MsgType::Error && before != AccountPhase::Online)) {
                auto handled = account_.handle(message); if (!handled) publishError(handled.error());
                if (message.type == MsgType::HelloResult) files_.setCacheSupported(message.body.value("ok",false) && message.body.value("file_cache",false));
                if(message.type==MsgType::HelloResult)socialSupported_=message.body.value("ok",false)&&message.body.value("profile_moments",false);
            } else {
                wire::require(isV2(message) && before == AccountPhase::Online);
                switch (message.type) {
                case MsgType::SocialResult: events_.publish(SocialEvent{message.body});break;
                case MsgType::FileCacheResult: {
                    auto result=files_.receiveCacheResult(message);if(!result)publishError(result.error());break;
                }
                case MsgType::Bundle: {
                    PrekeyBundle bundle{wire::text(message.body, "username", 32), wire::text(message.body, "sign_pub", 64),
                        wire::text(message.body, "dh_pub", 64), wire::text(message.body, "signed_prekey", 64), wire::text(message.body, "spk_sig", 128), std::nullopt};
                    const auto& otp = message.body.at("otp");
                    if (!otp.is_null()) bundle.otp = OneTimePrekey{wire::integer(otp, "id"), wire::text(otp, "pub", 64)};
                    auto result = chat_.handleBundle(bundle);
                    if (!result) {
                        if (result.error().code == ErrorCode::IdentityChanged) {
                            auto identity = chat_.offeredIdentity(bundle.username);
                            if (identity) events_.publish(identity.value());
                        } else publishError(result.error());
                    }
                    break;
                }
                case MsgType::RecvMsg: {
                    const auto envelope = wire::envelope(message);
                    if (envelope.kind == "chat") {
                        auto result = chat_.receive(envelope); if (!result) publishError(result.error());
                    } else {
                        auto result = envelope.kind == "file_meta" ? files_.receiveMetadata(envelope) :
                            envelope.kind == "file_chunk" ? files_.receiveChunk(envelope) :
                            envelope.kind == "file_ack" ? files_.receiveAck(envelope) :
                            Result<>::failure({ErrorCode::ProtocolError, "M3-05", "未知消息载荷类型"});
                        if (!result) publishError(result.error());
                    }
                    break;
                }
                case MsgType::MsgAck: {
                    wire::require(bodyBool(message.body, "accepted"));
                    auto result = chat_.handleAck(message.msgId, DeliveryState::ServerAccepted, wire::text(message.body, "to", 32));
                    if (!result) publishError(result.error());
                    break;
                }
                case MsgType::RecipientAck: {
                    auto result = chat_.handleAck(message.msgId, DeliveryState::RecipientReceived, wire::text(message.body, "from", 32));
                    if (!result) publishError(result.error());
                    break;
                }
                case MsgType::Presence: {
                    wire::require(message.body.at("online").is_boolean());
                    const auto username = wire::text(message.body, "username", 32); const bool online = bodyBool(message.body, "online");
                    if (!online_.count(username) || online_.at(username) != online) {
                        online_[username] = online; events_.publish(PresenceEvent{username, online});
                    }
                    if (!online && typingUntil_.erase(username)) events_.publish(TypingEvent{username, false});
                    break;
                }
                case MsgType::Typing: {
                    const auto username = wire::text(message.body, "from", 32);
                    if (online_.count(username) && online_.at(username)) {
                        if (!typingUntil_.count(username)) events_.publish(TypingEvent{username, true});
                        typingUntil_[username] = clock_() + 3000;
                    }
                    break;
                }
                case MsgType::UserList:
                case MsgType::ContactList: {
                    const auto& list = message.body.at("users"); wire::require(list.is_array() && list.size() <= 4096);
                    UserListEvent event;
                    std::map<std::string, bool> snapshot;
                    for (const auto& user : list) {
                        wire::require(user.at("online").is_boolean());
                        const auto username = wire::text(user, "username", 32); const bool online = bodyBool(user, "online");
                        wire::require(snapshot.emplace(username, online).second); event.users.emplace_back(username, online);
                    }
                    if (message.type == MsgType::UserList) for (auto it = typingUntil_.begin(); it != typingUntil_.end();) {
                        if (!snapshot.count(it->first) || !snapshot.at(it->first)) { events_.publish(TypingEvent{it->first, false}); it = typingUntil_.erase(it); }
                        else ++it;
                    }
                    if (message.type == MsgType::ContactList) {
                        ContactListEvent directory; directory.users = std::move(event.users);
                        for (auto [field, output] : {std::pair{"incoming", &directory.incoming}, std::pair{"outgoing", &directory.outgoing}}) {
                            const auto& requests = message.body.at(field); wire::require(requests.is_array() && requests.size() <= 1024);
                            for (const auto& name : requests) { wire::require(name.is_string() && !name.get_ref<const std::string&>().empty() && name.get_ref<const std::string&>().size() <= 32); output->push_back(name.get<std::string>()); }
                        }
                        for (const auto& [name, active] : snapshot) online_[name] = active;
                        events_.publish(std::move(directory));
                    } else { online_ = snapshot; events_.publish(event); }
                    break;
                }
                case MsgType::SearchResult: {
                    if (message.msgId != searchId_ || searchId_.empty()) break;
                    const auto& list = message.body.at("users"); wire::require(list.is_array() && list.size() <= 256);
                    StrangerListEvent event;
                    event.query = wire::text(message.body, "query", 32, true);
                    event.after = wire::text(message.body, "after", 32, true);
                    event.next = wire::text(message.body, "next", 32, true);
                    for (const auto& user : list) {
                        wire::require(user.at("online").is_boolean());
                        event.users.emplace_back(wire::text(user, "username", 32), bodyBool(user, "online"));
                    }
                    events_.publish(std::move(event)); break;
                }
                case MsgType::FriendResult: {
                    const auto username = wire::text(message.body, "username", 32, true);
                    const bool ok = bodyBool(message.body, "ok");
                    wire::require(message.body.at("ok").is_boolean());
                    events_.publish(FriendResultEvent{ok, username, bodyStr(message.body, "error"), bodyStr(message.body, "state")});
                    break;
                }
                case MsgType::UidResult: {
                    wire::require(account_.phase() == AccountPhase::Online && account_.uidLookupSupported());
                    const auto username = wire::text(message.body,"username",32);
                    const auto uid = wire::integer(message.body,"uid");
                    events_.publish(UserUidEvent{username,uid}); break;
                }
                case MsgType::GroupResult:
                case MsgType::GroupUpdate: {
                    wire::require(message.type != MsgType::GroupResult || bodyBool(message.body, "ok"));
                    GroupSnapshot group{wire::integer(message.body, "group_id"),
                        static_cast<std::uint64_t>(wire::integer(message.body, "revision")),
                        wire::text(message.body, "name", 128), {}};
                    const auto& members = message.body.at("members"); wire::require(members.is_array() && members.size() <= 64);
                    for (const auto& member : members) { wire::require(member.is_string()); group.members.push_back(member.get<std::string>()); }
                    auto result = groups_.applySnapshot(group);
                    if (result) events_.publish(group); else publishError(result.error());
                    break;
                }
                case MsgType::Error:
                    publishError({ErrorCode::ProtocolError, "M3-05", "服务器拒绝请求：" + wire::text(message.body, "code", 64)});
                    if (bodyStr(message.body, "code") == "kicked" || bodyStr(message.body, "code") == "password_reset") shutdown();
                    break;
                default: publishError({ErrorCode::ProtocolError, "M3-05", "未识别的服务器消息"});
                }
            }
        } catch (const std::exception&) { publishError({ErrorCode::ProtocolError, "M3-05", "服务器消息格式非法"}); }
        if (account_.phase() != before) {
            events_.publish(AccountEvent{account_.phase()});
            if (account_.phase() == AccountPhase::Online) {
                auto contacts = listContacts(); if (!contacts) publishError(contacts.error());
            }
        }
        if (account_.phase() == AccountPhase::Disconnected) break;
    }
    const auto now = wire::now();
    const auto monotonicNow = clock_();
    for (auto it = typingUntil_.begin(); it != typingUntil_.end();) {
        if (monotonicNow >= it->second) { events_.publish(TypingEvent{it->first, false}); it = typingUntil_.erase(it); }
        else ++it;
    }
    if (account_.phase() == AccountPhase::Online && now - lastFlushMs_ >= 1000) {
        auto sent = chat_.flushOutbox(); if (!sent) publishError(sent.error());
        lastFlushMs_ = now;
    }
    if (account_.phase() == AccountPhase::Online) {
        auto advanced = files_.tick(); if (!advanced) publishError(advanced.error());
        for (auto& event : files_.takeEvents()) events_.publish(std::move(event));
    }
    for (auto& event : chat_.takeEvents()) events_.publish(std::move(event));
    for (auto& event : chat_.takeStageEvents()) events_.publish(std::move(event));
    if (firstError) return Result<>::failure(*firstError);
    return Result<>::success({});
}
void ClientApplication::shutdown() noexcept {
    net_.close();
    if (tlsValidated_) { tlsValidated_ = false; events_.publish(ConnectionSecurity{}); }
    account_.connectionChanged(false);
    lastFlushMs_ = 0;
    groups_.clearTransient();
    for (const auto& [name, _] : typingUntil_) events_.publish(TypingEvent{name, false});
    typingUntil_.clear();
    if (!online_.empty()) { online_.clear(); events_.publish(UserListEvent{}); }
}
}
