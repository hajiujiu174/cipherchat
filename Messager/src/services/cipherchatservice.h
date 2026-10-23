#pragma once
#include "chatservice.h"
#include "cipherchatgateway.h"
#include "gameudp.h"
#include <QHash>
#include <QJsonArray>
namespace Messager {
class CipherChatService final : public ChatService {
    Q_OBJECT
  public:
    explicit CipherChatService(QJsonObject config, QObject* parent = nullptr);
    ~CipherChatService() override;
    bool isDemo() const override {
        return false;
    }
    bool gameRelaySupported() const override { return m_online && m_gameRelay; }
    bool sendGamePacket(const QString&, const QJsonObject&) override;
    QString gameTransport(const QString& peer) const override {return m_udp.ready(peer)?"UDP":"TLS";}
    QJsonObject gameTransportStats()const{return m_udp.stats();}
    bool m_gameRelay=false;
    bool online() const override {
        return m_online;
    }
    QString statusText() const override {
        return m_status;
    }
    QString username() const override {
        return m_user;
    }
    QJsonObject configuration() const override {
        return m_config;
    }
    const QVector<Conversation>& conversations() const override {
        return m_conversations;
    }
    const QVector<Transfer>& transfers() const override {
        return m_transfers;
    }
    QVector<Message> messages(const QString&) const override;
    Conversation conversation(const QString&) const override;
    QVector<Contact> contacts() const override {
        return m_contacts;
    }
    QVector<Contact> searchResults() const override {
        return m_searchResults;
    }
    QStringList incomingRequests() const override {
        return m_incoming;
    }
    QStringList outgoingRequests() const override {
        return m_outgoing;
    }
    bool friendRequestPending(const QString& user) const override {
        return m_pendingFriendRequests.contains(user);
    }
    QVector<Activity> activities() const override {
        return m_activities;
    }
    QString searchNext() const override {
        return m_searchNext;
    }
    void requestRegistrationCode(const QJsonObject&) override;
    void resetPassword(const QJsonObject&) override;
    void authenticate(const QJsonObject&) override;
    void disconnectAccount() override;
    QString send(const QString&, const QString&, bool = false) override;
    void retryMessage(const QString&) override;
    void markViewed(const QString&) override;
    void simulateIncoming(const QString&) override;
    void addTransfer(const QString&, qint64, const QString&) override;
    void sendFile(const QString&, const QString&) override;
    bool canSendFiles(const QString&) const override;
    QString openDirectConversation(const QString&) override;
    void transferAction(const QString&, const QString&) override;
    void searchUsers(const QString&, const QString& after = {}) override;
    void addFriend(const QString&) override;
    void respondFriend(const QString&, bool) override;
    void createGroup(const QString&, const QStringList&) override;
    void markActivitiesViewed() override;
    QJsonObject profile(const QString& user) const override { return m_profiles.value(user); }
    QJsonObject profileSummary(const QString& user) const override { return m_profileSummaries.value(user); }
    QJsonObject momentsFeed() const override { return m_momentsFeed; }
    void requestMomentsFeed(qint64 beforeMs=0,qint64 beforeId=0) override;
    void requestProfile(const QString&,qint64 before=0) override;
    void saveProfile(const QString&,const QString&) override;
    void publishMoment(const QString&,const QString&) override;

  private:
    void handle(const QJsonObject&);
    void ensurePeer(const QString&);
    void refreshPeerConversations();
    void applyRecord(const QJsonObject&, bool restored = false);
    void applyFile(const QJsonObject&, bool restored = false);
    void applyGroup(const QJsonObject&);
    void activity(const QString&, const QString&, const QString& = {});
    void error(const QString&);
    bool requireOnline();
    void resetView();
    void refreshProfileSummaries();
    void updateProfileSummary(const QString&, const QJsonObject&);
    CipherChatGateway m_gateway;
    GameUdp m_udp;
    QJsonObject m_config;
    QString m_user, m_status = "未连接 · 请登录 NEXTALK", m_viewing, m_searchNext;
    bool m_online = false;
    QVector<Conversation> m_conversations;
    QVector<Message> m_messages;
    QVector<Transfer> m_transfers;
    QVector<Contact> m_contacts, m_searchResults;
    QStringList m_incoming, m_outgoing, m_groupPending;
    QVector<Activity> m_activities;
    QHash<QString, QJsonObject> m_records, m_profiles;
    QHash<QString,quint64> m_profileGenerations;
    QHash<QString,QJsonObject> m_profileSummaries;
    QHash<QString,QString> m_summaryViews;
    QHash<QString,qint64> m_summaryRequestedAt;
    QJsonObject m_momentsFeed;
    quint64 m_feedGeneration=0;
    QHash<QString, QString> m_memberLogical;
    QHash<QString, QString> m_pendingFriendRequests;
    QHash<QString, QHash<QString, int>> m_groupDelivery;
};
} // namespace Messager
