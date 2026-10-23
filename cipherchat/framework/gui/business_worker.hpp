#pragma once
#include "ccfw/application.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ccfw::gui {
struct Authenticate {
    ConnectionOptions connection;
    std::filesystem::path dataDirectory;
    std::string username, password;
    bool registerNew = false;
    bool directEnabled = true;
    std::string accountServerId;
    std::string email, emailCode;
    bool requestCodeOnly = false;
    bool passwordReset = false;
    bool emailLogin = false;
};
struct SendFile { std::string peer; std::filesystem::path path; };
struct ConfirmIdentity { PeerIdentity identity; };
struct InspectIdentity { std::string peer; };
struct CreateGroup { std::string name; std::vector<std::string> members; };
struct SendGroup { std::int64_t id; std::string text; };
struct RetryGroup { std::string logicalId; };
struct TypeTo { std::string peer; };
struct ResumeFile { std::string id; };
struct CancelFile { std::string id; };
struct SetDirectEnabled { bool enabled; };
struct SearchUsers { std::string query; std::string after; };
struct AddFriend { std::string username; };
struct RespondFriend { std::string username; bool accept; };
struct Disconnect {};
struct SetWorkspace { WorkspaceState state; };
struct GameRelay { nlohmann::json value; };
struct SocialRequest { nlohmann::json value; };
struct LookupUid { std::string username; };
using Command = std::variant<Authenticate, SendText, SendFile, ConfirmIdentity, InspectIdentity,
    CreateGroup, SendGroup, RetryGroup, TypeTo, ResumeFile, CancelFile, SetDirectEnabled,
    SearchUsers, AddFriend, RespondFriend, Disconnect, SetWorkspace,
    CreateDeliveryBundle, ResumeDeliveryItem, CancelDeliveryBundle, ConfirmDelivery, LookupUid, SocialRequest, GameRelay>;
struct RestoredView {
    std::string username;
    PeerIdentity ownIdentity;
    std::vector<ChatRecord> history;
    std::vector<FileProgress> files;
    std::vector<GroupSnapshot> groups;
    AccountScope scope;
    WorkspaceState workspace;
    std::vector<DeliveryBundle> bundles;
    std::vector<MessageStageEvent> stages;
    std::vector<PeerIdentity> peerIdentities;
    std::vector<GroupDraft> groupDrafts;
    std::int64_t uid = 0;
    bool uidLookupSupported = false;
};
struct CommandResult {
    std::string requestId;
    bool accepted = false;
    std::optional<Error> error;
    std::string operationId;
};
struct RegistrationCodeEvent { bool ok; std::string email, error; int retryAfter = 0; bool passwordResetResult = false; };
using WorkerEvent = std::variant<ClientEvent, RestoredView, CommandResult, RegistrationCodeEvent>;

class BusinessWorker final : public IEventSink {
public:
    using TransportFactory = std::function<std::unique_ptr<ITransport>()>;
    explicit BusinessWorker(TransportFactory factory = {});
    ~BusinessWorker();
    BusinessWorker(const BusinessWorker&) = delete;
    BusinessWorker& operator=(const BusinessWorker&) = delete;
    // Correlation is supported for SendText, SendGroup, SendFile and
    // SetWorkspace. Unsupported tracked commands are rejected synchronously.
    bool post(Command command, std::string requestId = {});
    std::vector<WorkerEvent> drain();
    void requestStop() noexcept;
    void join();
    void publish(ClientEvent) override;
private:
    void run();
    void publishView(RestoredView);
    std::mutex mutex_;
    std::condition_variable wake_;
    struct QueuedCommand { Command command; std::string requestId; };
    std::deque<QueuedCommand> commands_, realtimeCommands_;
    std::atomic<std::int64_t> realtimeUntil_{0};
    void complete(const std::string&, std::optional<Error> = {}, std::string operationId = {});
    std::vector<WorkerEvent> events_;
    std::atomic<bool> stopping_{false}, overflow_{false}, authenticationFailed_{false};
    bool onlineViewReady_ = false; // Accessed only by the business thread.
    TransportFactory factory_;
    std::thread thread_;
};
}
