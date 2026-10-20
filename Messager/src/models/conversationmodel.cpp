#include "conversationmodel.h"
#include <QSet>
namespace Messager {
void ConversationFilter::setQuery(const QString& query) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_query = query;
    endFilterChange(Direction::Rows);
#else
    m_query = query;
    invalidateFilter();
#endif
}
void ConversationFilter::setMode(int mode) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_mode = mode;
    endFilterChange(Direction::Rows);
#else
    m_mode = mode;
    invalidateFilter();
#endif
}
ConversationModel::ConversationModel(ChatService* s, QObject* p) : QAbstractListModel(p), m_service(s) {
    m_rows = s->conversations();
    connect(s,&ChatService::profileSummaryChanged,this,&ConversationModel::refreshAvatar);
    for(const auto& row:m_rows)if(!row.group)refreshAvatar(row.peer);
    connect(s, &ChatService::conversationsChanged, this, [this] {
        const auto rows = m_service->conversations();
        bool structureChanged = rows.size() != m_rows.size();
        for (int i = 0; !structureChanged && i < rows.size(); ++i)
            structureChanged = rows[i].id != m_rows[i].id;
        if (structureChanged) {
            beginResetModel();
            m_rows = rows;
            endResetModel();
        } else {
            m_rows = rows;
            if (rowCount())
                emit dataChanged(index(0), index(rowCount() - 1));
        }
        QSet<QString> peers;
        for(const auto& row:m_rows)if(!row.group)peers.insert(row.peer);
        for(auto it=m_avatars.begin();it!=m_avatars.end();) {
            if(!peers.contains(it.key()))it=m_avatars.erase(it);else ++it;
        }
        for(const auto& user:peers)if(!m_avatars.contains(user))refreshAvatar(user);
    });
}
void ConversationModel::refreshAvatar(const QString& user) {
    QPixmap image;
    const auto data=QByteArray::fromBase64(m_service->profileSummary(user)["avatar"].toString().toLatin1());
    if(data.size()<=288*1024) {
        QImage decoded;
        if(decoded.loadFromData(data,"PNG")&&decoded.width()<=256&&decoded.height()<=256)
            image=QPixmap::fromImage(decoded).scaled(88,88,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    }
    m_avatars[user]=image;
    for(int row=0;row<m_rows.size();++row)
        if(!m_rows[row].group&&m_rows[row].peer==user)
            emit dataChanged(index(row),index(row),{AvatarImage,Name,Qt::DisplayRole});
}
int ConversationModel::rowCount(const QModelIndex& p) const {
    return p.isValid() ? 0 : m_rows.size();
}
QVariant ConversationModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() < 0 || i.row() >= rowCount())
        return {};
    // Own a snapshot: controllers may react before this model receives the change signal.
    const auto& c = m_rows.at(i.row());
    switch (role) {
    case Qt::DisplayRole:
    case Name:
        return c.group?c.name:m_service->profileSummary(c.peer).value("nickname").toString(c.name);
    case Id:
        return c.id;
    case Initials:
        return c.initials;
    case Color:
        return c.color;
    case Group:
        return c.group;
    case Preview:
        return c.preview;
    case Time:
        return c.time;
    case Unread:
        return c.unread;
    case AvatarImage:
        return c.group?QVariant{}:QVariant::fromValue(m_avatars.value(c.peer));
    default:
        return {};
    }
}
bool ConversationFilter::filterAcceptsRow(int r, const QModelIndex& p) const {
    const auto i = sourceModel()->index(r, 0, p);
    if (m_mode == 1 && i.data(ConversationModel::Unread).toInt() == 0)
        return false;
    if (m_mode == 2 && !i.data(ConversationModel::Group).toBool())
        return false;
    return m_query.isEmpty() ||
           i.data(ConversationModel::Name).toString().contains(m_query, Qt::CaseInsensitive) ||
           i.data(ConversationModel::Preview).toString().contains(m_query, Qt::CaseInsensitive);
}
} // namespace Messager
