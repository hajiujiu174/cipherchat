#pragma once
// 服务器消息路由：注册/登录状态机、公钥束分发、密文中转、离线队列、presence。
// 不直接接触 socket —— 通过 Peer 抽象投递下行消息，便于无头单元测试。

#include "common/protocol/messages.hpp"
#include "server/storage/storage.hpp"
#include "server/registration_email.hpp"
#include <memory>
#include "framework/include/ccfw/server_services.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cc::server {

// 服务器眼中的一条连接（由网络层适配实现）。
class Peer {
public:
    virtual ~Peer() = default;
    // 下发一条消息（网络层负责序列化成帧写出）
    virtual void deliver(const cc::proto::Message& msg) = 0;
    // 该连接已登录的用户名；未登录为空
    virtual const std::string& username() const = 0;
    // 登录成功时由路由器回填用户名（离线补投依赖它）
    virtual void setUsername(std::string name) = 0;
};

class Router {
public:
    // 注入时钟只用于确定性测试；默认使用单调时钟，不受系统时间调整影响。
    using Clock = std::function<std::int64_t()>;
    explicit Router(Storage& db, Clock clock = {});

    // 处理一条上行消息。peer 标识来源连接。
    void onMessage(Peer& peer, const cc::proto::Message& msg);

    // 连接断开：若已登录则广播 presence 离线并清理状态。
    void onDisconnect(Peer& peer);

    bool isOnline(const std::string& username) const;
    std::vector<std::string> onlineUsers() const;
    void setFileCacheEnabled(bool enabled) { fileCacheEnabled_=enabled; } // Before accepting connections.

    // Configure before accepting connections. Production always enables this gate.
    void requireRegistrationEmail(std::shared_ptr<RegistrationEmail> mail) { registrationEmail_ = std::move(mail); }
    void enablePasswordReset(std::shared_ptr<RegistrationEmail> mail) { passwordResetEmail_ = std::move(mail); }
    void enableEmailLogin(std::shared_ptr<RegistrationEmail> mail) { emailLogin_ = std::move(mail); }

private:
    std::shared_ptr<RegistrationEmail> registrationEmail_;
    std::shared_ptr<RegistrationEmail> passwordResetEmail_;
    std::shared_ptr<RegistrationEmail> emailLogin_;
    void handleEmailLogin(Peer& peer, const cc::proto::Message& msg);
    void completeLogin(Peer& peer, const Storage::Account& account);
    void handlePasswordReset(Peer& peer, const cc::proto::Message& msg);
    void handleRegister(Peer& peer, const cc::proto::Message& msg);
    void handleLoginRequest(Peer& peer, const cc::proto::Message& msg);
    void handleLoginResponse(Peer& peer, const cc::proto::Message& msg);
    void handleUploadPrekeys(Peer& peer, const cc::proto::Message& msg);
    void handleFetchBundle(Peer& peer, const cc::proto::Message& msg);
    void handleLookupUid(Peer& peer, const cc::proto::Message& msg);
    void handleSendMsg(Peer& peer, const cc::proto::Message& msg);
    void handleFileAck(Peer& peer, const cc::proto::Message& msg);
    void handleFileCache(Peer& peer, const cc::proto::Message& msg);
    void handleSocial(Peer& peer, const cc::proto::Message& msg);
    void handleRecipientAck(Peer& peer, const cc::proto::Message& msg);
    void deliver(Peer& peer, cc::proto::Message msg);
    bool usesV2(const Peer& peer) const;
    void handleTyping(Peer& peer, const cc::proto::Message& msg);
    void handleListUsers(Peer& peer);
    void handleListContacts(Peer& peer);
    void handleSearchUsers(Peer& peer, const cc::proto::Message& msg);
    void handleAddFriend(Peer& peer, const cc::proto::Message& msg);
    void handleRespondFriend(Peer& peer, const cc::proto::Message& msg);
    void deliverDirectory(Peer& peer);
    void handleCreateGroup(Peer& peer, const cc::proto::Message& msg);

    // 要求已登录，否则回 error 并返回 false
    bool requireLogin(Peer& peer, const char* action);
    // 投递给在线用户；不在线则入离线队列。返回是否在线投递。
    bool routeToUser(const std::string& username, const cc::proto::Message& msg);
    Peer* findOnline(const std::string& username) const;
    void deliverOfflineQueue(Peer& peer);
    // 连接标识只在服务器内使用，不接受客户端提供；断开时同步清理。
    static std::string connectionId(const Peer& peer);

    Storage& db_;
    bool fileCacheEnabled_ = true;
    Clock clock_;
    // 每连接一线程访问路由器；粗粒度互斥保证多语句存储序列不交错
    mutable std::mutex routeMutex_;
    std::map<std::string, Peer*> online_;      // username → 连接
    ccfw::server::ChallengeBook loginChallenges_;
    std::map<const Peer*, int> versions_;
    std::map<const Peer*, bool> directories_;
    std::map<const Peer*, std::map<std::string, std::size_t>> inFlight_;
    std::map<std::pair<const Peer*, std::string>, std::int64_t> lastTyping_;
};

}  // namespace cc::server
