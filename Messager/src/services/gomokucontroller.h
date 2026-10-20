#pragma once
#include "chatservice.h"
#include <QHash>
#include <QJsonArray>
#include <QPoint>
#include <QSet>
#include <array>
namespace Messager {
struct GomokuRoom {
    QString id, black, white, resigned;
    qint64 created = 0;
    int revision = 0, winner = 0;
    bool started = false, closed = false, pending = false;
    QVector<QPoint> moves;
    std::array<int, 225> board{};
};
class GomokuController final : public QObject {
    Q_OBJECT
  public:
    explicit GomokuController(ChatService* service, QObject* parent = nullptr);
    QList<GomokuRoom> rooms() const;
    GomokuRoom room(const QString& id) const {
        return m_rooms.value(id);
    }
    QString create(const QString& peer);
    bool join(const QString& id);
    bool move(const QString& id, int x, int y);
    void leave(const QString& id);
    void sync(const QString& id);
    bool canMove(const QString& id) const;
    bool openInvitation(const Message& message);
    QString lastError() const {
        return m_error;
    }
    static bool replay(GomokuRoom& room, const QJsonArray& moves);
  signals:
    void changed();
    void roomOpened(const QString& id);
    void error(const QString& text);

  private:
    void schedule();
    void scan();
    void session();
    bool send(const GomokuRoom& room, QJsonObject object);
    void sendState(GomokuRoom& room);
    bool fail(const QString& text);
    bool usable() const;
    bool expired(const GomokuRoom& room) const;
    ChatService* m_service;
    QHash<QString, GomokuRoom> m_rooms;
    QSet<QString> m_seen;
    QHash<QString, qint64> m_pendingSince;
    QString m_scope, m_error;
    bool m_scheduled = false;
};
} // namespace Messager
