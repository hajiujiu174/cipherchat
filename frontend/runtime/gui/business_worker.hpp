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
    std::string accountServerId;
    std::string email, emailCode;
    bool requestCodeOnly = false;
    bool passwordReset = false;
    bool emailLogin = false;
};
struct SendFile { std::string peer; std::filesystem::path path; std::int64_t groupId=0; };
struct CreateGroup { std::string name; std::vector<std::string> members; };
struct SendGroup { std::int64_t id; std::string text; };
struct RetryGroup { std::string logicalId; };
struct TypeTo { std::string peer; };
struct ResumeFile { std::string id; };
struct PauseFile { std::string id; };
struct CancelFile { std::string id; };
struct SearchUsers { std::string query; std::string after; };
struct AddFriend { std::string username; };
struct RespondFriend { std::string username; bool accept; };
struct Disconnect {};
struct SetWorkspace { WorkspaceState state; };
struct GameRelay { nlohmann::json value; };
struct SocialRequest { nlohmann::json value; };
struct LookupUid { std::string username; };
using Command = std::variant<Authenticate, SendText, SendFile,
    CreateGroup, SendGroup, RetryGroup, TypeTo, ResumeFile, PauseFile, CancelFile,
    SearchUsers, AddFriend, RespondFriend, Disconnect, SetWorkspace,
    CreateDeliveryBundle, ResumeDeliveryItem, CancelDeliveryBundle, ConfirmDelivery, LookupUid, SocialRequest, GameRelay>;
struct RestoredView {
    std::string username;
    std::vector<ChatRecord> history;
    std::vector<FileProgress> files;
    std::vector<GroupSnapshot> groups;
    AccountScope scope;
    WorkspaceState workspace;
    std::vector<DeliveryBundle> bundles;
    std::vector<MessageStageEvent> stages;
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
    // 构造客户端业务线程，初始化后续操作需要的状态或依赖。
    explicit BusinessWorker(TransportFactory factory = {});
    // 销毁客户端业务线程时释放它持有的资源。
    ~BusinessWorker();
    // 构造客户端业务线程，初始化后续操作需要的状态或依赖。
    BusinessWorker(const BusinessWorker&) = delete;
    // 将来源对象的状态赋值给当前对象。
    BusinessWorker& operator=(const BusinessWorker&) = delete;
    // Correlation is supported for SendText, SendGroup, SendFile and
    // SetWorkspace. Unsupported tracked commands are rejected synchronously.
    // 在状态和容量允许时把新命令放入业务队列，并唤醒处理线程。
    bool post(Command command, std::string requestId = {});
    // 取出累计的业务事件供界面消费，避免界面直接访问业务线程状态。
    std::vector<WorkerEvent> drain();
    // 请求Stop。
    void requestStop() noexcept;
    // 加入客户端业务线程。
    void join();
    // 发布客户端业务线程。
    void publish(ClientEvent) override;
private:
    // 循环处理桌面命令和网络业务事件，在业务线程内统一管理账号、服务和存储。
    void run();
    // 发布视图。
    void publishView(RestoredView);
    std::mutex mutex_;
    std::condition_variable wake_;
    struct QueuedCommand { Command command; std::string requestId; };
    std::deque<QueuedCommand> commands_, realtimeCommands_;
    std::atomic<std::int64_t> realtimeUntil_{0};
    // 按请求 ID 发布操作受理结果，让界面找到对应的待处理操作。
    void complete(const std::string&, std::optional<Error> = {}, std::string operationId = {});
    std::vector<WorkerEvent> events_;
    std::atomic<bool> stopping_{false}, overflow_{false}, authenticationFailed_{false};
    bool onlineViewReady_ = false; // Accessed only by the business thread.
    TransportFactory factory_;
    std::thread thread_;
};
}
