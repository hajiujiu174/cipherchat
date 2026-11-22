#pragma once
#include "services/chatservice.h"
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QPixmap>
#include <QSet>
namespace Messager {
class ConversationModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Roles { Id = Qt::UserRole + 1, Name, Initials, Color, Group, Preview, Time, Unread, AvatarImage, Pinned, LastActivity };
    // 构造会话列表模型，初始化后续操作需要的状态或依赖。
    explicit ConversationModel(ChatService* service, QObject* parent = nullptr);
    // 返回会话模型中可供列表读取的行数。
    int rowCount(const QModelIndex& parent = {}) const override;
    // 按照模型角色返回会话列表某一行需要显示的数据。
    QVariant data(const QModelIndex& index, int role) const override;
    // 修改指定会话的置顶状态并保存到当前账号的本机设置。
    void setPinned(const QString& id, bool pinned);

  private:
    ChatService* m_service;
    QVector<Conversation> m_rows;
    QHash<QString,QPixmap> m_avatars;
    // 刷新头像。
    void refreshAvatar(const QString& user);
    // 加载置顶项。
    void loadPins();
    // 计算当前账号和数据范围的置顶设置键。
    QString pinSettingsKey() const;
    QSet<QString> m_pins;
};
class ConversationFilter final : public QSortFilterProxyModel {
    Q_OBJECT
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    // 设置聊天消息使用的查询。
    void setQuery(const QString& query);
    // 设置聊天消息使用的模式。
    void setMode(int mode);

  protected:
    // 按会话的置顶、活动时间等排序规则比较两个模型条目。
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    // 筛选Accepts行。
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

  private:
    QString m_query;
    int m_mode = 0;
};
} // namespace Messager
