#pragma once
#include "chatservice.h"
#include "nextalkgateway.h"
#include "features/games/gameudp.h"
#include <QHash>
#include <QJsonArray>
namespace Messager {
class NextalkService final : public ChatService {
    Q_OBJECT
  public:
    // 构造桌面联机服务，初始化后续操作需要的状态或依赖。
    explicit NextalkService(QJsonObject config, QObject* parent = nullptr);
    // 销毁桌面联机服务时释放它持有的资源。
    ~NextalkService() override;
    // 判断演示条件是否成立。
    bool isDemo() const override {
        return false;
    }
    // 返回桌面联机服务使用的游戏中转受支持。
    bool gameRelaySupported() const override { return m_online && m_gameRelay; }
    // 为允许联机的好友选择游戏传输路径，实时状态在 UDP 可用时优先使用 UDP 中转。
    bool sendGamePacket(const QString&, const QJsonObject&) override;
    // 返回桌面联机服务使用的游戏传输通道。
    QString gameTransport(const QString& peer) const override {return m_udp.ready(peer)?"UDP":"TLS";}
    // 返回桌面联机服务使用的游戏传输通道统计信息。
    QJsonObject gameTransportStats()const{return m_udp.stats();}
    bool m_gameRelay=false;
    // 判断桌面联机服务的是否已在线。
    bool online() const override {
        return m_online;
    }
    // 返回桌面联机服务的当前状态的显示文字。
    QString statusText() const override {
        return m_status;
    }
    // 返回桌面联机服务使用的用户名。
    QString username() const override {
        return m_user;
    }
    // 返回桌面联机服务使用的配置。
    QJsonObject configuration() const override {
        return m_config;
    }
    // 返回桌面联机服务使用的会话列表。
    const QVector<Conversation>& conversations() const override {
        return m_conversations;
    }
    // 返回桌面联机服务使用的传输任务列表。
    const QVector<Transfer>& transfers() const override {
        return m_transfers;
    }
    // 返回指定会话的本地记录，并在展示时应用编辑、撤回和已读控制消息。
    QVector<Message> messages(const QString&) const override;
    // 返回桌面联机服务使用的会话。
    Conversation conversation(const QString&) const override;
    // 返回桌面联机服务使用的联系人。
    QVector<Contact> contacts() const override {
        return m_contacts;
    }
    // 搜索结果列表。
    QVector<Contact> searchResults() const override {
        return m_searchResults;
    }
    // 返回桌面联机服务使用的收到的申请。
    QStringList incomingRequests() const override {
        return m_incoming;
    }
    // 返回桌面联机服务使用的发出的申请。
    QStringList outgoingRequests() const override {
        return m_outgoing;
    }
    // 返回桌面联机服务使用的好友申请待处理。
    bool friendRequestPending(const QString& user) const override {
        return m_pendingFriendRequests.contains(user);
    }
    // 返回桌面联机服务使用的活动通知。
    QVector<Activity> activities() const override {
        return m_activities;
    }
    // 搜索下一项。
    QString searchNext() const override {
        return m_searchNext;
    }
    // 请求注册验证码。
    void requestRegistrationCode(const QJsonObject&) override;
    // 重置密码。
    void resetPassword(const QJsonObject&) override;
    // 检查登录请求和证书，准备重连所需状态并向业务线程提交认证命令。
    void authenticate(const QJsonObject&) override;
    // 主动断开当前账号、作废自动重连并刷新桌面状态。
    void disconnectAccount() override;
    // 为当前会话生成请求 ID，通过网关提交单聊或群聊发送，并先显示待发送气泡。
    QString send(const QString&, const QString&, bool = false) override;
    // 复用原消息标识提交私聊重试，群聊按逻辑消息标识继续原任务。
    void retryMessage(const QString&) override;
    // 更新会话的本地阅读位置和未读计数，并在条件满足时提交已读事件。
    void markViewed(const QString&) override;
    // 模拟收到的。
    void simulateIncoming(const QString&) override;
    // 添加传输。
    void addTransfer(const QString&, qint64, const QString&) override;
    // 检查当前会话及本地文件，提交私聊附件或按群成员分别提交文件任务。
    void sendFile(const QString&, const QString&) override;
    // 根据会话类型和当前好友或群成员关系判断是否开放文件发送。
    bool canSendFiles(const QString&) const override;
    // 建立或选择指定对端的本地私聊会话条目。
    QString openDirectConversation(const QString&) override;
    // 将文件卡片的暂停、继续或取消操作映射为运行时命令。
    void transferAction(const QString&, const QString&) override;
    // 搜索用户。
    void searchUsers(const QString&, const QString& after = {}) override;
    // 添加好友。
    void addFriend(const QString&) override;
    // 回应好友。
    void respondFriend(const QString&, bool) override;
    // 创建群聊。
    void createGroup(const QString&, const QStringList&) override;
    // 标记活动通知已查看。
    void markActivitiesViewed() override;
    // 返回桌面联机服务使用的资料。
    QJsonObject profile(const QString& user) const override { return m_profiles.value(user); }
    // 返回桌面联机服务使用的用户资料摘要。
    QJsonObject profileSummary(const QString& user) const override { return m_profileSummaries.value(user); }
    // 返回桌面联机服务使用的动态信息流。
    QJsonObject momentsFeed() const override { return m_momentsFeed; }
    // 请求动态信息流。
    void requestMomentsFeed(qint64 beforeMs=0,qint64 beforeId=0) override;
    // 请求资料。
    void requestProfile(const QString&,qint64 before=0) override;
    // 将昵称和头像编辑结果作为资料更新请求提交给服务端。
    void saveProfile(const QString&,const QString&) override;
    // 发布动态。
    void publishMoment(const QString&,const QString&) override;
    // 返回桌面联机服务使用的social操作。
    void socialAction(const QJsonObject&) override;
    // 为自己发送的文字消息生成编辑或撤回事件，交由原会话发送。
    void changeMessage(const QString&,const QString&,bool) override;

  private:
    // 按业务事件类型更新账号、会话、联系人、文件及桌面提示。
    void handle(const QJsonObject&);
    // 在本地会话列表中确保存在这位对端的私聊条目。
    void ensurePeer(const QString&);
    // 从当前对端列表更新私聊会话的显示信息。
    void refreshPeerConversations();
    // 把框架返回的消息映射为桌面记录，按消息 ID 去重并聚合群聊签收。
    void applyRecord(const QJsonObject&, bool restored = false);
    // 将运行时文件进度投影为桌面的传输条目和聊天文件气泡。
    void applyFile(const QJsonObject&, bool restored = false);
    // 应用群聊。
    void applyGroup(const QJsonObject&);
    // 向桌面活动列表添加一条通知，并触发界面更新。
    void activity(const QString&, const QString&, const QString& = {});
    // 返回桌面联机服务的失败时的错误信息。
    void error(const QString&);
    // 要求在线。
    bool requireOnline();
    // 重置视图。
    void resetView();
    // 刷新资料Summaries。
    void refreshProfileSummaries();
    // 更新用户资料摘要。
    void updateProfileSummary(const QString&, const QJsonObject&);
    // 返回桌面联机服务使用的已查看状态键。
    QString viewStateKey() const;
    // 保存已查看状态。
    void saveViewState();
    // 加载已查看状态。
    void loadViewState();
    // 在会话意外中断后按退避间隔重连，跳过已作废的定时任务。
    void scheduleReconnect();
    QHash<QString,qint64> m_readBefore;
    QHash<QString,qint64> m_receiptsSent;
    QJsonObject m_reconnectRequest;
    int m_reconnectAttempt=0;
    quint64 m_reconnectGeneration=0;
    bool m_hadOnline=false;
    bool m_viewStateLoaded=false;
    NextalkGateway m_gateway;
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
