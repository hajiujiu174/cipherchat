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
    explicit ConversationModel(ChatService* service, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    void setPinned(const QString& id, bool pinned);

  private:
    ChatService* m_service;
    QVector<Conversation> m_rows;
    QHash<QString,QPixmap> m_avatars;
    void refreshAvatar(const QString& user);
    void loadPins();
    QString pinSettingsKey() const;
    QSet<QString> m_pins;
};
class ConversationFilter final : public QSortFilterProxyModel {
    Q_OBJECT
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setQuery(const QString& query);
    void setMode(int mode);

  protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

  private:
    QString m_query;
    int m_mode = 0;
};
} // namespace Messager
