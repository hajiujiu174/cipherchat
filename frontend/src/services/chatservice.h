#pragma once
#include "core/types.h"
#include <QJsonObject>
#include <QObject>
namespace Messager {
// UI-facing contract. A production implementation consumes NEXTALK worker events.
// MockService is one implementation, deliberately isolated from authentication/networking.
class ChatService : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    // 供界面读取当前的会话列表。
    virtual const QVector<Conversation>& conversations() const = 0;
    // 供界面读取当前的消息。
    virtual QVector<Message> messages(const QString& conversation) const = 0;
    // 供界面读取当前的传输任务列表。
    virtual const QVector<Transfer>& transfers() const = 0;
    // 返回聊天服务使用的会话。
    virtual Conversation conversation(const QString& id) const = 0;
    // 发送聊天服务。
    virtual QString send(const QString& conversation, const QString& text, bool fail = false) = 0;
    // 重试消息。
    virtual void retryMessage(const QString& id) = 0;
    // 标记已查看。
    virtual void markViewed(const QString& conversation) = 0;
    // 模拟收到的。
    virtual void simulateIncoming(const QString& conversation) = 0;
    // 添加传输。
    virtual void addTransfer(const QString& name, qint64 bytes, const QString& conversation) = 0;
    // 发送文件。
    virtual void sendFile(const QString& path, const QString& conversation) = 0;
    // 判断聊天服务当前是否允许Send文件。
    virtual bool canSendFiles(const QString& id) const {
        const auto c = conversation(id);
        return !c.id.isEmpty() && !c.group;
    }
    // 打开私聊会话。
    virtual QString openDirectConversation(const QString&) {
        return {};
    }
    // 传输操作。
    virtual void transferAction(const QString& id, const QString& action) = 0;
    // 判断演示条件是否成立。
    virtual bool isDemo() const {
        return true;
    }
    // 返回聊天服务使用的游戏中转受支持。
    virtual bool gameRelaySupported() const { return false; }
    // 返回聊天服务使用的游戏传输通道。
    virtual QString gameTransport(const QString&) const { return "TLS"; }
    // 发送游戏数据包。
    virtual bool sendGamePacket(const QString&, const QJsonObject&) { return false; }
    // 判断聊天服务的是否已在线。
    virtual bool online() const {
        return false;
    }
    // 返回聊天服务的当前状态的显示文字。
    virtual QString statusText() const {
        return "本地演示 · 未连接网络";
    }
    // 返回聊天服务使用的用户名。
    virtual QString username() const {
        return {};
    }
    // 返回聊天服务使用的配置。
    virtual QJsonObject configuration() const {
        return {};
    }
    // 返回聊天服务使用的own身份。
    virtual QJsonObject ownIdentity() const {
        return {};
    }
    // 返回聊天服务使用的联系人。
    virtual QVector<Contact> contacts() const {
        return {};
    }
    // 搜索结果列表。
    virtual QVector<Contact> searchResults() const {
        return {};
    }
    // 返回聊天服务使用的收到的申请。
    virtual QStringList incomingRequests() const {
        return {};
    }
    // 返回聊天服务使用的发出的申请。
    virtual QStringList outgoingRequests() const {
        return {};
    }
    // 返回聊天服务使用的好友申请待处理。
    virtual bool friendRequestPending(const QString&) const {
        return false;
    }
    // 判断好友条件是否成立。
    bool isFriend(const QString& username) const {
        for (const auto& contact : contacts())
            if (contact.username == username && contact.friendContact)
                return true;
        return false;
    }
    // 返回聊天服务使用的活动通知。
    virtual QVector<Activity> activities() const {
        return {};
    }
    // 搜索下一项。
    virtual QString searchNext() const {
        return {};
    }
    // 请求注册验证码。
    virtual void requestRegistrationCode(const QJsonObject&) { emit operationError("当前模式不发送邮箱验证码"); }
    // 重置密码。
    virtual void resetPassword(const QJsonObject&) { emit operationError("当前模式不支持邮箱重置密码"); }
    // 认证聊天服务。
    virtual void authenticate(const QJsonObject&) {
        emit operationError("演示模式不进行身份认证");
    }
    // 断开账号。
    virtual void disconnectAccount() {
        emit operationError("演示模式没有服务器连接");
    }
    // 搜索用户。
    virtual void searchUsers(const QString&, const QString& = {}) {
        emit operationError("演示模式不查询服务器");
    }
    // 添加好友。
    virtual void addFriend(const QString&) {
        emit operationError("演示模式不发送好友申请");
    }
    // 回应好友。
    virtual void respondFriend(const QString&, bool) {
        emit operationError("演示模式不处理好友申请");
    }
    // 创建群聊。
    virtual void createGroup(const QString&, const QStringList&) {
        emit operationError("演示模式不创建服务器群组");
    }


    // 返回聊天服务使用的资料。
    virtual QJsonObject profile(const QString&) const { return {}; }
    // 返回聊天服务使用的用户资料摘要。
    virtual QJsonObject profileSummary(const QString& user) const { return profile(user); }
    // 返回聊天服务使用的动态信息流。
    virtual QJsonObject momentsFeed() const { return {}; }
    // 请求动态信息流。
    virtual void requestMomentsFeed(qint64=0,qint64=0) { emit momentsFeedFinished(false,"请登录后查看朋友圈"); }
    // 请求资料。
    virtual void requestProfile(const QString&, qint64=0) { emit operationError("当前模式没有服务器个人资料"); }
    // 保存资料。
    virtual void saveProfile(const QString&, const QString&) { emit operationError("当前模式无法保存资料"); }
    // 发布动态。
    virtual void publishMoment(const QString&, const QString&) { emit operationError("当前模式无法发布朋友圈"); }
    // 标记活动通知已查看。
    virtual void markActivitiesViewed() {}
    // 更新聊天服务中的social操作状态。
    virtual void socialAction(const QJsonObject&) { emit operationError("请登录后操作"); }
    // 更改消息。
    virtual void changeMessage(const QString&,const QString&,bool) { emit operationError("当前模式不支持修改消息"); }
  signals:
    // 返回聊天服务使用的social结果。
    void socialResult(const QJsonObject&);
    // 通知监听方动态信息流发生变化，供界面或调用方刷新。
    void momentsFeedChanged();
    // 返回聊天服务使用的动态信息流Finished。
    void momentsFeedFinished(bool ok,const QString& error);
    // 通知监听方资料发生变化，供界面或调用方刷新。
    void profileChanged(const QString& username);
    // 通知监听方用户资料摘要发生变化，供界面或调用方刷新。
    void profileSummaryChanged(const QString& username);
    // 返回聊天服务使用的资料操作Finished。
    void profileOperationFinished(const QString& op,const QString& username,bool ok,const QString& error);
    // 返回聊天服务使用的游戏数据包。
    void gamePacket(const QString& peer, const QJsonObject& packet);
    // 通知监听方游戏中转发生变化，供界面或调用方刷新。
    void gameRelayChanged();
    // 通知监听方会话列表发生变化，供界面或调用方刷新。
    void conversationsChanged();
    // 通知监听方消息发生变化，供界面或调用方刷新。
    void messagesChanged(const QString& conversation);
    // 通知监听方传输任务列表发生变化，供界面或调用方刷新。
    void transfersChanged();
    // 通知监听方会话发生变化，供界面或调用方刷新。
    void sessionChanged();
    // 返回聊天服务使用的注册验证码结果。
    void registrationCodeResult(bool ok, const QString& email, const QString& error, int retryAfter);
    // 返回聊天服务使用的密码重置结果。
    void passwordResetResult(bool ok, const QString& error);
    // 通知监听方联系人发生变化，供界面或调用方刷新。
    void contactsChanged();
    // 通知监听方活动通知发生变化，供界面或调用方刷新。
    void activitiesChanged();
    // 返回聊天服务使用的操作错误。
    void operationError(const QString& message);
};
} // namespace Messager
