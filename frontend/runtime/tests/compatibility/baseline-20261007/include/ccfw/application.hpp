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
using ClientEvent = std::variant<AccountEvent, ChatRecord, FileProgress,
                                 PresenceEvent, TypingEvent, GroupSnapshot, GroupSendReport, PeerIdentity,
                                 UserListEvent, ContactListEvent, StrangerListEvent, FriendResultEvent, Error>;
// 队列由 GUI 适配器持有；publish 只入队。GUI 线程 drain 后刷新 Widgets。
class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void publish(ClientEvent) = 0;
};
class ClientApplication {
public:
    using Clock = std::function<std::int64_t()>;
    ClientApplication(ITransport& net, AccountService& account, ChatService& chat,
                      FileTransferService& files, GroupService& groups, IEventSink& events, Clock clock = {});
    Result<> connect(const ConnectionOptions&);
    Result<> tick(); // 单业务线程，有限批次 poll，不能在 GUI 线程阻塞
    Result<> listContacts();
    Result<> searchUsers(const std::string& query, const std::string& after = {});
    Result<> addFriend(const std::string& username);
    Result<> respondFriend(const std::string& username, bool accept);
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
};
} // namespace ccfw
