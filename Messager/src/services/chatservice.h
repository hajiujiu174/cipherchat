#pragma once
#include "core/types.h"
#include <QJsonObject>
#include <QObject>
namespace Messager {
// UI-facing contract. A production implementation consumes CipherChat worker events.
// MockService is one implementation, deliberately isolated from authentication/networking.
class ChatService : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual const QVector<Conversation>& conversations() const = 0;
    virtual QVector<Message> messages(const QString& conversation) const = 0;
    virtual const QVector<Transfer>& transfers() const = 0;
    virtual Conversation conversation(const QString& id) const = 0;
    virtual QString send(const QString& conversation, const QString& text, bool fail = false) = 0;
    virtual void retryMessage(const QString& id) = 0;
    virtual void markViewed(const QString& conversation) = 0;
    virtual void simulateIncoming(const QString& conversation) = 0;
    virtual void addTransfer(const QString& name, qint64 bytes, const QString& conversation) = 0;
    virtual void sendFile(const QString& path, const QString& conversation) = 0;
    virtual bool canSendFiles(const QString& id) const {
        const auto c = conversation(id);
        return !c.id.isEmpty() && !c.group;
    }
    virtual QString openDirectConversation(const QString&) {
        return {};
    }
    virtual void transferAction(const QString& id, const QString& action) = 0;
    virtual bool isDemo() const {
        return true;
    }
    virtual bool gameRelaySupported() const { return false; }
    virtual QString gameTransport(const QString&) const { return "TLS"; }
    virtual bool sendGamePacket(const QString&, const QJsonObject&) { return false; }
    virtual bool online() const {
        return false;
    }
    virtual QString statusText() const {
        return "本地演示 · 未连接网络";
    }
    virtual QString username() const {
        return {};
    }
    virtual QJsonObject configuration() const {
        return {};
    }
    virtual QJsonObject ownIdentity() const {
        return {};
    }
    virtual QVector<Contact> contacts() const {
        return {};
    }
    virtual QVector<Contact> searchResults() const {
        return {};
    }
    virtual QStringList incomingRequests() const {
        return {};
    }
    virtual QStringList outgoingRequests() const {
        return {};
    }
    virtual bool friendRequestPending(const QString&) const {
        return false;
    }
    bool isFriend(const QString& username) const {
        for (const auto& contact : contacts())
            if (contact.username == username && contact.friendContact)
                return true;
        return false;
    }
    virtual QVector<Activity> activities() const {
        return {};
    }
    virtual QString searchNext() const {
        return {};
    }
    virtual void requestRegistrationCode(const QJsonObject&) { emit operationError("当前模式不发送邮箱验证码"); }
    virtual void resetPassword(const QJsonObject&) { emit operationError("当前模式不支持邮箱重置密码"); }
    virtual void authenticate(const QJsonObject&) {
        emit operationError("演示模式不进行身份认证");
    }
    virtual void disconnectAccount() {
        emit operationError("演示模式没有服务器连接");
    }
    virtual void searchUsers(const QString&, const QString& = {}) {
        emit operationError("演示模式不查询服务器");
    }
    virtual void addFriend(const QString&) {
        emit operationError("演示模式不发送好友申请");
    }
    virtual void respondFriend(const QString&, bool) {
        emit operationError("演示模式不处理好友申请");
    }
    virtual void createGroup(const QString&, const QStringList&) {
        emit operationError("演示模式不创建服务器群组");
    }


    virtual QJsonObject profile(const QString&) const { return {}; }
    virtual QJsonObject profileSummary(const QString& user) const { return profile(user); }
    virtual QJsonObject momentsFeed() const { return {}; }
    virtual void requestMomentsFeed(qint64=0,qint64=0) { emit momentsFeedFinished(false,"请登录后查看朋友圈"); }
    virtual void requestProfile(const QString&, qint64=0) { emit operationError("当前模式没有服务器个人资料"); }
    virtual void saveProfile(const QString&, const QString&) { emit operationError("当前模式无法保存资料"); }
    virtual void publishMoment(const QString&, const QString&) { emit operationError("当前模式无法发布朋友圈"); }
    virtual void markActivitiesViewed() {}
  signals:
    void momentsFeedChanged();
    void momentsFeedFinished(bool ok,const QString& error);
    void profileChanged(const QString& username);
    void profileSummaryChanged(const QString& username);
    void profileOperationFinished(const QString& op,const QString& username,bool ok,const QString& error);
    void gamePacket(const QString& peer, const QJsonObject& packet);
    void gameRelayChanged();
    void conversationsChanged();
    void messagesChanged(const QString& conversation);
    void transfersChanged();
    void sessionChanged();
    void registrationCodeResult(bool ok, const QString& email, const QString& error, int retryAfter);
    void passwordResetResult(bool ok, const QString& error);
    void contactsChanged();
    void activitiesChanged();
    void operationError(const QString& message);
};
} // namespace Messager
