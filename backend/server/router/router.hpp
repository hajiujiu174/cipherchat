#pragma once
// 服务器消息路由：普通账号注册/登录、消息中转、离线队列、presence；连接由 TLS 保护。
// 不直接接触 socket —— 通过 Peer 抽象投递下行消息，便于无头单元测试。

#include "common/protocol/messages.hpp"
#include "server/storage/storage.hpp"
#include "server/registration_email.hpp"
#include <memory>
#include "ccfw/server_services.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cc::server {

// 服务器眼中的一条连接（由网络层适配实现）。
class Peer {
public:
    // 销毁服务端连接接口时释放它持有的资源。
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
    // 构造服务端协议路由器，初始化后续操作需要的状态或依赖。
    explicit Router(Storage& db, Clock clock = {});

    // 处理一条上行消息。peer 标识来源连接。
    void onMessage(Peer& peer, const cc::proto::Message& msg);

    // 连接断开：若已登录则广播 presence 离线并清理状态。
    void onDisconnect(Peer& peer);

    // 判断在线条件是否成立。
    bool isOnline(const std::string& username) const;
    // 返回服务端协议路由器使用的在线用户。
    std::vector<std::string> onlineUsers() const;
    // 设置服务端协议路由器使用的服务端文件缓存已启用。
    void setFileCacheEnabled(bool enabled) { fileCacheEnabled_=enabled; } // Before accepting connections.

    // Configure before accepting connections. Production always enables this gate.
    // 要求注册邮箱。
    void requireRegistrationEmail(std::shared_ptr<RegistrationEmail> mail) { registrationEmail_ = std::move(mail); }
    // 启用密码重置。
    void enablePasswordReset(std::shared_ptr<RegistrationEmail> mail) { passwordResetEmail_ = std::move(mail); }
    // 启用邮箱验证码登录。
    void enableEmailLogin(std::shared_ptr<RegistrationEmail> mail) { emailLogin_ = std::move(mail); }

private:
    std::shared_ptr<RegistrationEmail> registrationEmail_;
    std::shared_ptr<RegistrationEmail> passwordResetEmail_;
    std::shared_ptr<RegistrationEmail> emailLogin_;
    // 核对绑定邮箱与登录验证码，通过后将当前连接关联到原账号。
    void handleEmailLogin(Peer& peer, const cc::proto::Message& msg);
    // 把认证成功的账号绑定到连接，发出初始状态并补投未签收消息。
    void completeLogin(Peer& peer, const Storage::Account& account);
    // 核对绑定邮箱验证码，更新密码验证信息并撤销原有在线会话。
    void handlePasswordReset(Peer& peer, const cc::proto::Message& msg);
    // 校验注册请求与邮箱验证码，通过后在服务端创建账号。
    void handleRegister(Peer& peer, const cc::proto::Message& msg);
    // 校验 TLS 连接提交的密码，通过后绑定账号。
    void handleLoginRequest(Peer& peer, const cc::proto::Message& msg);
    // 处理LookupUID。
    void handleLookupUid(Peer& peer, const cc::proto::Message& msg);
    // 使用登录连接确定发件人，校验收件和群聊权限，再持久化服务端待投递记录。
    void handleSendMsg(Peer& peer, const cc::proto::Message& msg);
    // 处理文件块进度确认。
    void handleFileAck(Peer& peer, const cc::proto::Message& msg);
    // 分派文件缓存的登记、写块、读取和取消操作，并返回结果。
    void handleFileCache(Peer& peer, const cc::proto::Message& msg);
    // 按操作分派好友、群管理、资料、应用同步、通话或 UDP 票据请求。
    void handleSocial(Peer& peer, const cc::proto::Message& msg);
    // 处理通话。
    void handleCall(Peer&,const cc::proto::Message&);
    struct Call {std::string caller,callee;bool active=false;std::int64_t touched=0;std::map<std::string,std::pair<std::int64_t,std::uint64_t>> streams;};
    std::map<std::string,Call> calls_;
    // 按登录接收者和消息 ID 验证签收，更新投递队列并通知在线发件人。
    void handleRecipientAck(Peer& peer, const cc::proto::Message& msg);
    // 投递服务端协议路由器。
    void deliver(Peer& peer, cc::proto::Message msg);
    // 查询该连接是否协商为协议版本 2。
    bool usesV2(const Peer& peer) const;
    // 校验游戏对端、好友关系及转发频率，将请求转发给在线接收者。
    void handleGameRelay(Peer&, const cc::proto::Message&);
    std::map<const Peer*, std::int64_t> lastGameRelay_;
    std::map<const Peer*, bool> gamePeers_;
    // 处理正在输入。
    void handleTyping(Peer& peer, const cc::proto::Message& msg);
    // 处理列表用户。
    void handleListUsers(Peer& peer);
    // 处理列表联系人。
    void handleListContacts(Peer& peer);
    // 处理搜索用户。
    void handleSearchUsers(Peer& peer, const cc::proto::Message& msg);
    // 处理Add好友。
    void handleAddFriend(Peer& peer, const cc::proto::Message& msg);
    // 处理Respond好友。
    void handleRespondFriend(Peer& peer, const cc::proto::Message& msg);
    // 投递目录。
    void deliverDirectory(Peer& peer);
    // 验证创建者、群名及成员列表，持久化并向在线成员发送群信息。
    void handleCreateGroup(Peer& peer, const cc::proto::Message& msg);

    // 要求已登录，否则回 error 并返回 false
    bool requireLogin(Peer& peer, const char* action);
    // 投递给在线用户；不在线则入离线队列。返回是否在线投递。
    bool routeToUser(const std::string& username, const cc::proto::Message& msg);
    // 查找在线。
    Peer* findOnline(const std::string& username) const;
    // 读取当前账号待投递的消息，按在途容量补发并等待客户端签收。
    void deliverOfflineQueue(Peer& peer);

    Storage& db_;
    bool fileCacheEnabled_ = true;
    Clock clock_;
    // 每连接一线程访问路由器；粗粒度互斥保证多语句存储序列不交错
    mutable std::mutex routeMutex_;
    std::map<std::string, Peer*> online_;      // username → 连接
    std::map<const Peer*, int> versions_;
    std::map<const Peer*, bool> directories_;
    std::map<const Peer*, std::map<std::string, std::size_t>> inFlight_;
    std::map<std::pair<const Peer*, std::string>, std::int64_t> lastTyping_;
};

}  // namespace cc::server
