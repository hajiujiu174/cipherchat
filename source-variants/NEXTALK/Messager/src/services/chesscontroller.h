#pragma once
#include "chatservice.h"
#include "chessrules.h"
#include <QHash>
#include <QJsonArray>
#include <QPoint>
#include <QSet>
#include <array>
namespace Messager {
struct ChessRoom:ChessState {
    QString id, black, white, resigned;
    qint64 created=0;
    int revision=0;
    bool started=false,closed=false,pending=false;
};
class ChessController final : public QObject {
    Q_OBJECT
  public:
    explicit ChessController(ChatService* service, QObject* parent = nullptr, const QString& game = "chess");
    QString gameId() const { return m_gameId; }
    QList<ChessRoom> rooms() const;
    ChessRoom room(const QString& id) const {
        return m_rooms.value(id);
    }
    QString create(const QString& peer);
    bool join(const QString& id);
    bool move(const QString& id,const QString& code);
    void leave(const QString& id);
    void sync(const QString& id);
    bool canMove(const QString& id) const;
    bool openInvitation(const Message& message);
    QString lastError() const {
        return m_error;
    }
    static bool replay(ChessRoom& room, const QJsonArray& moves);
    static QStringList legalMoves(const ChessRoom& room);
  signals:
    void changed();
    void roomOpened(const QString& id);
    void error(const QString& text);

  private:
    void schedule();
    void scan();
    void session();
    bool send(const ChessRoom& room, QJsonObject object);
    void sendState(ChessRoom& room);
    bool fail(const QString& text);
    bool usable() const;
    bool expired(const ChessRoom& room) const;
    ChatService* m_service;
    QHash<QString, ChessRoom> m_rooms;
    QSet<QString> m_seen;
    QHash<QString, qint64> m_pendingSince;
    QString m_scope, m_error, m_gameId;
    bool m_scheduled = false;
};
} // namespace Messager
