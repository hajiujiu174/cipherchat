#pragma once
#include "services/chatservice.h"
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
    // 构造棋类房间控制器，初始化后续操作需要的状态或依赖。
    explicit ChessController(ChatService* service, QObject* parent = nullptr, const QString& game = "chess");
    // 返回棋类房间控制器使用的游戏类型 ID。
    QString gameId() const { return m_gameId; }
    // 返回目前能在联机界面显示的房间列表。
    QList<ChessRoom> rooms() const;
    // 返回棋类房间控制器使用的房间。
    ChessRoom room(const QString& id) const {
        return m_rooms.value(id);
    }
    // 创建棋类房间控制器。
    QString create(const QString& peer);
    // 加入棋类房间控制器。
    bool join(const QString& id);
    // 移动棋类房间控制器。
    bool move(const QString& id,const QString& code);
    // 离开棋类房间控制器。
    void leave(const QString& id);
    // 同步棋类房间控制器。
    void sync(const QString& id);
    // 判断棋类房间控制器当前是否允许走法。
    bool canMove(const QString& id) const;
    // 打开邀请。
    bool openInvitation(const Message& message);
    // 返回棋类房间控制器使用的last错误。
    QString lastError() const {
        return m_error;
    }
    // 重放棋类房间控制器。
    static bool replay(ChessRoom& room, const QJsonArray& moves);
    // 返回棋类房间控制器使用的可用走法。
    static QStringList legalMoves(const ChessRoom& room);
  signals:
    // 通知订阅方组件状态已经变化。
    void changed();
    // 返回棋类房间控制器使用的房间已打开。
    void roomOpened(const QString& id);
    // 返回棋类房间控制器的失败时的错误信息。
    void error(const QString& text);

  private:
    // 安排联机房间的延时处理，以合并连续更新。
    void schedule();
    // 扫描棋类房间控制器。
    void scan();
    // 返回棋类房间控制器使用的会话。
    void session();
    // 发送棋类房间控制器。
    bool send(const ChessRoom& room, QJsonObject object);
    // 发送状态。
    void sendState(ChessRoom& room);
    // 生成失败结果棋类房间控制器。
    bool fail(const QString& text);
    // 判断给定的棋类房间控制器数据是否可用。
    bool usable() const;
    // 判断给定的棋类房间控制器数据是否过期。
    bool expired(const ChessRoom& room) const;
    ChatService* m_service;
    QHash<QString, ChessRoom> m_rooms;
    QSet<QString> m_seen;
    QHash<QString, qint64> m_pendingSince;
    QString m_scope, m_error, m_gameId;
    bool m_scheduled = false;
};
} // namespace Messager
