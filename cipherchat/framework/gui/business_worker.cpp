#include "business_worker.hpp"
#include "../src/wire_helpers.hpp"
#include "../src/snapshot_codec.hpp"
#include "../src/feature_codec.hpp"
#include "ccfw/delivery_service.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <type_traits>
#include <set>

namespace ccfw::gui {
namespace {
void wipe(std::string& text) { if (!text.empty()) OPENSSL_cleanse(text.data(), text.size()); text.clear(); }
void wipeCommand(Command& command) { if (auto* auth = std::get_if<Authenticate>(&command)) { wipe(auth->password); wipe(auth->emailCode); } }
std::string profileName(const AccountScope& scope) {
    const auto digest = cc::crypto::sha256(scope.server + "/" + scope.username);
    static constexpr char alphabet[] = "0123456789abcdef"; std::string result;
    for (unsigned char byte : digest) { result += alphabet[byte >> 4]; result += alphabet[byte & 15]; }
    return result;
}
struct Services {
    PlatformSecretProtector protector; SqliteLocalStore store{protector};
    std::unique_ptr<ITransport> ownedNet; ITransport& net;
    SessionService sessions{store}; AccountService account{net, store}; ChatService chat{net, sessions, store};
    DirectFileChannel direct; FileTransferService files{net, store, sessions, &direct}; GroupService groups;
    DeliveryService delivery{store, chat, files};
    ClientApplication app;
    Services(const Authenticate& request, IEventSink& events, const BusinessWorker::TransportFactory& factory)
        : ownedNet(factory ? factory() : std::make_unique<TlsTransport>()), net(*ownedNet),
          groups(chat, net, scope(request), &store), app(net, account, chat, files, groups, events) {
        files.setDirectEnabled(request.directEnabled);
        const auto base=scope(request); const auto root=request.dataDirectory;
        auto bind=[this,base,root](const std::string& username)->Result<> {
            auto canonical=base;canonical.username=username;
            const auto directory=root/profileName(canonical);std::filesystem::create_directories(directory);
            auto opened=store.open(directory/"client.db",canonical);if(!opened)return opened;
            groups.bindAccount(canonical);
            return files.setDownloadDirectory(directory/"downloads");
        };
        if(request.username.find('@')!=std::string::npos && !request.registerNew && (!request.requestCodeOnly || request.emailLogin) && !request.passwordReset)
            account.setLoginAlias(request.username,std::move(bind));
        else { auto opened=bind(request.username);if(!opened)throw std::runtime_error(opened.error().message); }
    }
    ~Services() { app.shutdown(); }
    static AccountScope scope(const Authenticate& request) {
        return {request.accountServerId.empty()
            ? request.connection.host + ":" + std::to_string(request.connection.port) + "/" + request.connection.expectedServerName
            : request.accountServerId, request.username};
    }
    Result<RestoredView> view() {
        auto loaded = store.load(); if (!loaded) return Result<RestoredView>::failure(loaded.error());
        RestoredView view; view.username = loaded.value().account.username; view.history = loaded.value().history;
        view.scope = loaded.value().account; view.workspace = loaded.value().features.workspace; view.bundles = loaded.value().features.bundles; view.stages = chat.stages();
        view.peerIdentities = loaded.value().trustedPeers; view.groupDrafts = loaded.value().groupDrafts;
        view.uid = account.userId(); view.uidLookupSupported = account.uidLookupSupported();
        std::set<std::string> observed; for (const auto& e : view.stages) observed.insert(e.messageId);
        for (auto it = view.history.rbegin(); it != view.history.rend() && observed.size() < 500; ++it) if (observed.insert(it->messageId).second) {
            const auto stage = it->state == DeliveryState::RecipientReceived ? MessageStage::RecipientReceived : it->state == DeliveryState::ServerAccepted ? MessageStage::ServerAccepted : it->state == DeliveryState::Failed ? MessageStage::Failed : MessageStage::LocalQueued;
            view.stages.push_back({it->messageId, stage, 0, -1, 0, -1});
        }
        if (loaded.value().identity) view.ownIdentity = {view.username, loaded.value().identity->signing.pub, loaded.value().identity->identityDh.pub, true};
        for (const auto& transfer : loaded.value().transfers) {
            auto progress = files.progress(transfer.fileId); if (!progress) return Result<RestoredView>::failure(progress.error()); view.files.push_back(progress.value());
        }
        auto snapshots = groups.snapshots(); if (!snapshots) return Result<RestoredView>::failure(snapshots.error()); view.groups = snapshots.value();
        return Result<RestoredView>::success(std::move(view));
    }
};
}
BusinessWorker::BusinessWorker(TransportFactory factory) : factory_(std::move(factory)), thread_([this] { run(); }) {}
BusinessWorker::~BusinessWorker() { requestStop(); join(); }
bool BusinessWorker::post(Command command, std::string requestId) {
    const bool tracked = std::holds_alternative<SendText>(command) || std::holds_alternative<SendGroup>(command) || std::holds_alternative<SendFile>(command) || std::holds_alternative<SetWorkspace>(command);
    if (!requestId.empty() && !tracked) { wipeCommand(command); return false; }
    std::lock_guard lock(mutex_);
    if (stopping_ || commands_.size() >= 64) { wipeCommand(command); return false; }
    commands_.push_back({std::move(command), std::move(requestId)}); wake_.notify_one(); return true;
}
std::vector<WorkerEvent> BusinessWorker::drain() {
    std::lock_guard lock(mutex_); auto result = std::move(events_); events_.clear(); return result;
}
void BusinessWorker::requestStop() noexcept { stopping_ = true; wake_.notify_all(); }
void BusinessWorker::join() { if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join(); }
void BusinessWorker::publish(ClientEvent event) {
    if (const auto* account = std::get_if<AccountEvent>(&event)) {
        if (account->phase == AccountPhase::Online && !onlineViewReady_) return;
        if (account->phase != AccountPhase::Online) onlineViewReady_ = false;
    }
    if (const auto* error = std::get_if<Error>(&event); error && error->task == "M3-02") authenticationFailed_ = true;
    std::lock_guard lock(mutex_);
    if (const auto* progress = std::get_if<FileProgress>(&event)) {
        for (auto& queued : events_) if (auto* client = std::get_if<ClientEvent>(&queued))
            if (auto* old = std::get_if<FileProgress>(client); old && old->fileId == progress->fileId) { *old = *progress; return; }
    }
    if (events_.size() >= 1024) { overflow_ = true; return; }
    events_.emplace_back(std::in_place_type<ClientEvent>, std::move(event));
}
void BusinessWorker::publishView(RestoredView view) {
    std::lock_guard lock(mutex_);
    if (events_.size() >= 1024) { overflow_ = true; return; }
    events_.emplace_back(std::move(view));
}
void BusinessWorker::complete(const std::string& id, std::optional<Error> error, std::string operationId) {
    if (id.empty()) return;
    std::lock_guard lock(mutex_);
    if (events_.size() >= 1024) { overflow_ = true; return; }
    events_.emplace_back(CommandResult{id, !error, std::move(error), std::move(operationId)});
}
void BusinessWorker::run() {
    std::unique_ptr<Services> services;
    std::optional<Authenticate> pending;
    enum class Stage { None, Protocol, Registration, Code }; Stage stage = Stage::None;
    bool wasOnline = false;
    auto clearPending = [&] { if (pending) { wipe(pending->password); wipe(pending->emailCode); } pending.reset(); stage = Stage::None; };
    auto result = [&](const Result<>& value) { if (!value) publish(value.error()); };
    auto restoreView = [&] { auto restored = services->view(); if (restored) { publishView(std::move(restored.value())); return true; } publish(restored.error()); return false; };
    while (!stopping_) {
        std::optional<QueuedCommand> queued;
        std::optional<Command> command;
        std::string requestId;
        { std::unique_lock lock(mutex_); wake_.wait_for(lock, std::chrono::milliseconds(10), [&] { return stopping_ || !commands_.empty(); });
          if (!commands_.empty()) { queued = std::move(commands_.front()); commands_.pop_front(); command = std::move(queued->command); requestId = std::move(queued->requestId); } }
        if (stopping_) { if (command) wipeCommand(*command); break; }
        try {
            if (command) {
                auto finish = [&](const auto& value) { complete(requestId, value ? std::optional<Error>{} : std::optional<Error>{value.error()}); };
                std::visit([&](auto& request) {
                    using T = std::decay_t<decltype(request)>;
                    if constexpr (std::is_same_v<T, Authenticate>) {
                        if (request.username.empty() || request.username.size() > (request.username.find('@')==std::string::npos ? 32u : 254u) || request.dataDirectory.empty()) {
                            publish(Error{ErrorCode::InvalidArgument, "M4-04", "请填写账号和数据目录"}); wipe(request.password); return;
                        }
                        clearPending(); services.reset(); authenticationFailed_ = false; wasOnline = false; onlineViewReady_ = false;
                        services = std::make_unique<Services>(request, *this, factory_);
                        if(request.username.find('@')==std::string::npos || request.registerNew || (request.requestCodeOnly && !request.emailLogin) || request.passwordReset)restoreView();
                        auto connected = services->app.connect(request.connection);
                        if (!connected) { wipe(request.password); return; }
                        pending = std::move(request); stage = Stage::Protocol;
                    } else if constexpr (std::is_same_v<T, SetDirectEnabled>) {
                        if (services) services->files.setDirectEnabled(request.enabled);
                    } else {
                        if (!services) { Error error{ErrorCode::NotConnected, "M4-04", "请先登录账号"}; publish(error); complete(requestId, error); return; }
                        if constexpr (std::is_same_v<T, SendText>) { auto sent = services->chat.sendText(request); complete(requestId, sent ? std::optional<Error>{} : std::optional<Error>{sent.error()}, sent ? sent.value() : std::string{}); if (!sent) publish(sent.error()); }
                        else if constexpr (std::is_same_v<T, SendFile>) { auto sent = services->files.start(request.peer, request.path); finish(sent); if (!sent) publish(sent.error()); }
                        else if constexpr (std::is_same_v<T, ConfirmIdentity> || std::is_same_v<T, InspectIdentity>) {
                            publish(Error{ErrorCode::NotImplemented,"ordinary-chat","普通聊天不使用身份指纹"});
                        } else if constexpr (std::is_same_v<T, CreateGroup>) result(services->groups.create(request.name, request.members));
                        else if constexpr (std::is_same_v<T, SendGroup>) { auto sent = services->groups.sendText(request.id, request.text); finish(sent); if (sent) publish(sent.value()); else publish(sent.error()); }
                        else if constexpr (std::is_same_v<T, RetryGroup>) { auto sent = services->groups.retry(request.logicalId); if (sent) publish(sent.value()); else publish(sent.error()); }
                        else if constexpr (std::is_same_v<T, TypeTo>) result(services->groups.sendTyping(request.peer));
                        else if constexpr (std::is_same_v<T, SearchUsers>) result(services->app.searchUsers(request.query, request.after));
                        else if constexpr (std::is_same_v<T, SocialRequest>) result(services->app.social(request.value));
                        else if constexpr (std::is_same_v<T, LookupUid>) result(services->app.lookupUid(request.username));
                        else if constexpr (std::is_same_v<T, AddFriend>) result(services->app.addFriend(request.username));
                        else if constexpr (std::is_same_v<T, RespondFriend>) result(services->app.respondFriend(request.username, request.accept));
                        else if constexpr (std::is_same_v<T, ResumeFile>) result(services->files.resume(request.id));
                        else if constexpr (std::is_same_v<T, CancelFile>) result(services->files.cancel(request.id));
                        else if constexpr (std::is_same_v<T, CreateDeliveryBundle>) { auto created = services->delivery.create(request); if (!created) publish(created.error()); }
                        else if constexpr (std::is_same_v<T, ResumeDeliveryItem>) result(services->delivery.resume(request));
                        else if constexpr (std::is_same_v<T, CancelDeliveryBundle>) result(services->delivery.cancel(request));
                        else if constexpr (std::is_same_v<T, ConfirmDelivery>) result(services->delivery.confirm(request));
                        else if constexpr (std::is_same_v<T, SetWorkspace>) {
                            try { features::validateWorkspace(request.state); }
                            catch (...) { Error error{ErrorCode::InvalidArgument, "M10-05", "工作台状态超过限制，请减少保留的草稿数量"}; publish(error); complete(requestId, error); return; }
                            auto loaded = services->store.load(); if (!loaded) { publish(loaded.error()); complete(requestId, loaded.error()); return; }
                            auto next = loaded.value();
                            if (nlohmann::json(next.features.workspace) != nlohmann::json(request.state)) {
                                next.features.workspace = request.state; const auto expected = next.revision++;
                                auto saved = services->store.commit(expected, next); result(saved); finish(saved); if (saved) publish(request.state);
                            } else complete(requestId);
                        }
                        else if constexpr (std::is_same_v<T, Disconnect>) { clearPending(); services->app.shutdown(); services->direct.close(); wasOnline = false; publish(AccountEvent{AccountPhase::Disconnected}); }
                    }
                }, *command);
                wipeCommand(*command);
                if (services) {
                    for (auto& event : services->chat.takeEvents()) publish(std::move(event));
                    for (auto& event : services->chat.takeStageEvents()) publish(std::move(event));
                    for (auto& event : services->delivery.takeEvents()) publish(std::move(event));
                    for (auto& event : services->files.takeEvents()) publish(std::move(event));
                }
            }
            if (services) {
                (void)services->app.tick();
                if (services->account.phase() == AccountPhase::Online) {
                    result(services->delivery.tick());
                    for (auto& event : services->delivery.takeEvents()) publish(std::move(event));
                }
                if (authenticationFailed_.exchange(false)) clearPending();
                if (auto response = services->account.takeRegistrationCodeResult()) {
                    RegistrationCodeEvent event{cc::proto::bodyBool(response->body,"ok"), cc::proto::bodyStr(response->body,"email"),
                        cc::proto::bodyStr(response->body,"error"), int(std::clamp<std::int64_t>(cc::proto::bodyInt(response->body,"retry_after"),0,3600)), response->type == cc::proto::MsgType::PasswordResetResult};
                    { std::lock_guard lock(mutex_); if (events_.size() < 1024) events_.emplace_back(std::move(event)); else overflow_ = true; }
                    clearPending();
                }
                if (pending && services->account.phase() == AccountPhase::Connected && stage != Stage::Code) {
                    if (stage == Stage::Protocol && pending->requestCodeOnly) {
                        auto requested = services->account.requestRegistrationCode(pending->username, pending->email, pending->passwordReset, pending->emailLogin);
                        result(requested); if (requested) stage = Stage::Code; else clearPending();
                    } else if (stage == Stage::Protocol && pending->passwordReset) {
                        auto reset = services->account.resetPassword(pending->username,pending->email,pending->emailCode,pending->password);
                        wipe(pending->password); wipe(pending->emailCode);
                        result(reset); if (reset) stage=Stage::Code; else clearPending();
                    } else if (stage == Stage::Protocol && pending->emailLogin) {
                        result(services->account.loginWithEmailCode(pending->email,pending->emailCode));clearPending();
                    } else if (stage == Stage::Protocol && pending->registerNew) {
                        auto registered = services->account.registerAccount(pending->username, pending->password, pending->email, pending->emailCode);
                        result(registered); if (registered) stage = Stage::Registration; else clearPending();
                    } else if (stage == Stage::Protocol || stage == Stage::Registration) {
                        result(services->account.login(pending->username, pending->password)); clearPending();
                    }
                    publish(AccountEvent{services->account.phase()});
                }
                const bool online = services->account.phase() == AccountPhase::Online;
                if (online && !wasOnline) {
                    if (restoreView()) {
                        onlineViewReady_ = true;
                        publish(AccountEvent{AccountPhase::Online});

                    } else services->app.shutdown();
                }
                wasOnline = services->account.phase() == AccountPhase::Online;
            }
            if (overflow_.exchange(false)) {
                clearPending(); if (services) services->app.shutdown();
                { std::lock_guard lock(mutex_); events_.clear(); }
                publish(Error{ErrorCode::Forbidden, "M4-04", "界面事件队列已满，请重新登录加载已保存的历史"});
                publish(AccountEvent{AccountPhase::Disconnected});
            }
        } catch (const std::exception&) {
            if (command) wipeCommand(*command);
            clearPending();
            Error error{ErrorCode::StorageFailure, "M4-04", "操作失败，请检查连接配置、数据目录和文件状态"}; publish(error); complete(requestId, error);
            if (services) services->app.shutdown();
            publish(AccountEvent{AccountPhase::Disconnected});
        }
    }
    clearPending(); services.reset();
    std::lock_guard lock(mutex_); for (auto& command : commands_) wipeCommand(command.command); commands_.clear();
}
}
