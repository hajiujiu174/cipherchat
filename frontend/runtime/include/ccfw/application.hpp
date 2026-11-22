#pragma once
#include "ccfw/account_service.hpp"
#include "ccfw/chat_service.hpp"
#include "ccfw/file_transfer.hpp"
#include "ccfw/group_service.hpp"
#include <variant>

namespace ccfw {
struct AccountEvent { AccountPhase phase; };
struct PresenceEvent { std::string username; bool online; };
struct TypingEvent { std::string username; bool active = true; };
struct UserListEvent { std::vector<std::pair<std::string, bool>> users; };
struct ContactListEvent { std::vector<std::pair<std::string, bool>> users; std::vector<std::string> incoming, outgoing; };
struct StrangerListEvent { std::vector<std::pair<std::string, bool>> users; std::string query, after, next; };
struct FriendResultEvent { bool ok; std::string username; std::string error; std::string state; };
struct GameRelayEvent { nlohmann::json value; };
struct SocialEvent { nlohmann::json value; };
struct UserUidEvent { std::string username; std::int64_t uid = 0; };
using ClientEvent = std::variant<AccountEvent, ChatRecord, FileProgress,
                                 PresenceEvent, TypingEvent, GroupSnapshot, GroupSendReport,
                                 UserListEvent, ContactListEvent, StrangerListEvent, FriendResultEvent,
                                 MessageStageEvent, ConnectionSecurity, WorkspaceState, DeliveryBundle, Error, UserUidEvent, SocialEvent, GameRelayEvent>;
// 队列由 GUI 适配器持有；publish 只入队。GUI 线程 drain 后刷新 Widgets。
class IEventSink {
public:
    // 销毁客户端事件接收接口时释放它持有的资源。
    virtual ~IEventSink() = default;
    // 发布客户端事件接收接口。
    virtual void publish(ClientEvent) = 0;
};
class ClientApplication {
public:
    using Clock = std::function<std::int64_t()>;
    // 构造客户端应用协调器，初始化后续操作需要的状态或依赖。
    ClientApplication(ITransport& net, AccountService& account, ChatService& chat,
                      FileTransferService& files, GroupService& groups, IEventSink& events, Clock clock = {});
    // 建立传输连接并启动应用协议能力协商。
    Result<> connect(const ConnectionOptions&);
    // 轮询网络消息并按类型分派，随后推进在线的发件箱和文件任务。
    Result<> tick(); // 单业务线程，有限批次 poll，不能在 GUI 线程阻塞
    // 列出联系人。
    Result<> listContacts();
    // 返回客户端应用协调器使用的游戏中转。
    Result<> gameRelay(nlohmann::json request);
    // 将资料或社交功能请求转为协议消息，通过客户端当前连接发送。
    Result<> social(nlohmann::json request);
    // 查找UID。
    Result<> lookupUid(const std::string& username);
    // 搜索用户。
    Result<> searchUsers(const std::string& query, const std::string& after = {});
    // 添加好友。
    Result<> addFriend(const std::string& username);
    // 回应好友。
    Result<> respondFriend(const std::string& username, bool accept);
    // 关闭客户端应用协调器。
    void shutdown() noexcept;
private:
    ITransport& net_;
    AccountService& account_;
    ChatService& chat_;
    FileTransferService& files_;
    GroupService& groups_;
    IEventSink& events_;
    Clock clock_;
    std::uint64_t searchSequence_ = 0;
    std::string searchId_;
    std::map<std::string, bool> online_;
    std::map<std::string, std::int64_t> typingUntil_;
    std::int64_t lastFlushMs_ = 0;
    bool tlsValidated_ = false;
    bool socialSupported_ = false;
    bool gameRelaySupported_ = false;
};
} // namespace ccfw
