#include "gomokucontroller.h"
#include "../../third_party/gomoku/rules.h"
#include "gomokuwire.h"
#include <QDateTime>
#include <QJsonArray>
#include <QTimer>
#include <QUuid>
#include <algorithm>
namespace Messager {
namespace {
QJsonArray movesJson(const GomokuRoom& r) {
    QJsonArray result;
    for (auto p : r.moves)
        result.append(QJsonArray{p.x(), p.y()});
    return result;
}
bool integral(const QJsonValue& v, int low, int high) {
    return v.isDouble() && v.toDouble() >= low && v.toDouble() <= high && v.toDouble() == v.toInt();
}
} // namespace
GomokuController::GomokuController(ChatService* service, QObject* parent)
    : QObject(parent), m_service(service) {
    connect(service, &ChatService::messagesChanged, this, [this] { schedule(); });
    connect(service, &ChatService::conversationsChanged, this, [this] { schedule(); });
    connect(service, &ChatService::sessionChanged, this, &GomokuController::session);
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this] {
        for (auto it = m_pendingSince.begin(); it != m_pendingSince.end();) {
            if (QDateTime::currentMSecsSinceEpoch() - it.value() > 12000) {
                const auto id = it.key();
                it = m_pendingSince.erase(it);
                m_rooms[id].pending = false;
                fail("等待对方确认超时，可点击重新同步；对方需保持客户端在线。");
                emit changed();
            } else
                ++it;
        }
    });
    timer->start();
    session();
}
bool GomokuController::usable() const {
    return m_service->online() && !m_service->isDemo() && !m_service->username().isEmpty();
}
bool GomokuController::expired(const GomokuRoom& r) const {
    return QDateTime::currentMSecsSinceEpoch() - r.created > 24 * 60 * 60 * 1000LL;
}
bool GomokuController::fail(const QString& text) {
    m_error = text;
    emit error(text);
    return false;
}
void GomokuController::schedule() {
    if (m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        scan();
    });
}
void GomokuController::session() {
    const auto scope =
        m_service->username() + "|" +
        QString::fromUtf8(QJsonDocument(m_service->configuration()).toJson(QJsonDocument::Compact));
    if (scope != m_scope) {
        m_scope = scope;
        m_rooms.clear();
        m_seen.clear();
        m_pendingSince.clear();
        m_error.clear();
        emit changed();
    }
    schedule();
}
QList<GomokuRoom> GomokuController::rooms() const {
    auto result = m_rooms.values();
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.created > b.created; });
    return result;
}
bool GomokuController::replay(GomokuRoom& r, const QJsonArray& moves) {
    if (moves.size() > 225)
        return false;
    r.moves.clear();
    r.board.fill(0);
    r.winner = 0;
    for (const auto& value : moves) {
        if (!value.isArray() || value.toArray().size() != 2 || r.winner)
            return false;
        auto p = value.toArray();
        if (!integral(p[0], 0, 14) || !integral(p[1], 0, 14))
            return false;
        int x = p[0].toInt(), y = p[1].toInt();
        if (r.board[y * 15 + x])
            return false;
        int player = r.moves.size() % 2 + 1;
        r.board[y * 15 + x] = player;
        r.moves.append({x, y});
        if (GomokuRules::won(r.board, x, y, player))
            r.winner = player;
    }
    if (r.moves.size() == 225 && !r.winner)
        r.winner = 3;
    return true;
}
bool GomokuController::send(const GomokuRoom& room, QJsonObject o) {
    if (!usable())
        return fail("请登录并保持在线后使用双人五子棋。");
    const auto peer = m_service->username() == room.black ? room.white : room.black;
    if (!m_service->isFriend(peer))
        return fail("仅可与当前好友进行在线对弈。");
    const auto conversation = m_service->openDirectConversation(peer);
    if (conversation.isEmpty())
        return fail("无法打开好友会话。");
    o["room"] = room.id;
    o["black"] = room.black;
    o["white"] = room.white;
    o["created"] = room.created;
    const auto id = m_service->send(conversation, GomokuWire::encode(o));
    if (id.isEmpty())
        return fail("消息未提交，请检查连接后重新同步。");
    return true;
}
void GomokuController::sendState(GomokuRoom& r) {
    ++r.revision;
    send(r, {{"type", "state"},
             {"seq", r.revision},
             {"started", r.started},
             {"closed", r.closed},
             {"resigned", r.resigned},
             {"moves", movesJson(r)}});
}
QString GomokuController::create(const QString& peer) {
    if (!usable()) {
        fail("请先登录真实账号，演示模式不提供在线对弈。");
        return {};
    }
    if (peer == m_service->username() || !m_service->isFriend(peer)) {
        fail("请选择一位好友发送邀请。");
        return {};
    }
    GomokuRoom r;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
    r.black = m_service->username();
    r.white = peer;
    r.created = QDateTime::currentMSecsSinceEpoch();
    if (!send(r, {{"type", "invite"}}))
        return {};
    m_rooms[r.id] = r;
    m_error.clear();
    emit changed();
    emit roomOpened(r.id);
    return r.id;
}
bool GomokuController::join(const QString& id) {
    if (!m_rooms.contains(id))
        return fail("邀请尚未同步或房间不存在。");
    auto& r = m_rooms[id];
    if (r.closed || r.winner || expired(r))
        return fail("该房间已结束或邀请已过期，请重新邀请。");
    if (m_service->username() != r.white)
        return fail("只有受邀好友可以加入这个房间。");
    if (send(r, {{"type", "join"}})) {
        m_error.clear();
        r.pending = true;
        m_pendingSince[id] = QDateTime::currentMSecsSinceEpoch();
        emit changed();
        emit roomOpened(id);
        return true;
    }
    return false;
}
bool GomokuController::canMove(const QString& id) const {
    const auto r = m_rooms.value(id);
    if (!usable() || r.id.isEmpty() || !r.started || r.closed || r.winner || r.pending || expired(r))
        return false;
    return (r.moves.size() % 2 == 0 ? r.black : r.white) == m_service->username();
}
bool GomokuController::move(const QString& id, int x, int y) {
    if (!canMove(id))
        return fail("尚未轮到你，或正在等待对方确认。");
    auto& r = m_rooms[id];
    if (x < 0 || x > 14 || y < 0 || y > 14 || r.board[y * 15 + x])
        return fail("该位置不能落子。");
    m_error.clear();
    if (m_service->username() == r.black) {
        auto updated = r;
        auto moves = movesJson(r);
        moves.append(QJsonArray{x, y});
        if (!replay(updated, moves))
            return false;
        r = updated;
        sendState(r);
        emit changed();
        return true;
    }
    if (send(r, {{"type", "move"}, {"ply", int(r.moves.size())}, {"x", x}, {"y", y}})) {
        r.pending = true;
        m_pendingSince[id] = QDateTime::currentMSecsSinceEpoch();
        emit changed();
        return true;
    }
    return false;
}
void GomokuController::leave(const QString& id) {
    if (!m_rooms.contains(id))
        return;
    auto& r = m_rooms[id];
    if (r.closed || r.winner)
        return;
    if (m_service->username() == r.black) {
        r.closed = true;
        r.resigned = r.black;
        sendState(r);
        emit changed();
    } else if (send(r, {{"type", "leave"}})) {
        r.pending = true;
        m_pendingSince[id] = QDateTime::currentMSecsSinceEpoch();
        emit changed();
    }
}
void GomokuController::sync(const QString& id) {
    if (!m_rooms.contains(id))
        return;
    auto& r = m_rooms[id];
    m_error.clear();
    r.pending = false;
    m_pendingSince.remove(id);
    if (m_service->username() == r.black)
        sendState(r);
    else
        send(r, {{"type", "sync"}});
    emit changed();
}
bool GomokuController::openInvitation(const Message& m) {
    const auto o = GomokuWire::decode(m.text);
    if (o["type"] != "invite")
        return false;
    scan();
    const auto id = o["room"].toString();
    if (!m_rooms.contains(id))
        return fail("邀请无效或尚未同步，请稍后重试。");
    const auto r = m_rooms[id];
    if ((m.outgoing ? m_service->username() : m.sender) != r.black || o["black"] != r.black ||
        o["white"] != r.white)
        return fail("邀请来源不匹配。");
    emit roomOpened(id);
    if (m_service->username() == m_rooms[id].white && !m_rooms[id].started)
        return join(id);
    return true;
}
void GomokuController::scan() {
    if (!usable())
        return;
    struct Event {
        Message message;
        QJsonObject object;
        QString peer;
    };
    QList<Event> events;
    const auto me = m_service->username();
    for (const auto& c : m_service->conversations()) {
        if (c.group || c.peer.isEmpty())
            continue;
        for (const auto& m : m_service->messages(c.id)) {
            if (m.file || m.state == SendState::Failed)
                continue;
            const auto o = GomokuWire::decode(m.text);
            if (o.isEmpty())
                continue;
            if (!m.outgoing && m.sender != c.peer)
                continue;
            events.append({m, o, c.peer});
        }
    }
    bool changedState = false;
    // Restore invitations and authoritative snapshots before handling pending requests.
    for (const auto& e : events) {
        const auto& o = e.object;
        if (o["type"] != "invite")
            continue;
        const auto id = o["room"].toString(), black = o["black"].toString(), white = o["white"].toString();
        const auto sender = e.message.outgoing ? me : e.message.sender;
        if (sender != black || black == white || black.size() > 64 || white.size() > 64 ||
            !((black == me && white == e.peer) || (white == me && black == e.peer)))
            continue;
        const auto created = o["created"].toInteger();
        if (created <= 0 || created > QDateTime::currentMSecsSinceEpoch() + 300000)
            continue;
        if (!m_rooms.contains(id)) {
            GomokuRoom r;
            r.id = id;
            r.black = black;
            r.white = white;
            r.created = created;
            m_rooms[id] = r;
            changedState = true;
        }
    }
    for (const auto& e : events) {
        const auto& o = e.object;
        const auto id = o["room"].toString();
        if (o["type"] != "state" || !m_rooms.contains(id))
            continue;
        auto& old = m_rooms[id];
        const auto sender = e.message.outgoing ? me : e.message.sender;
        if (sender != old.black || o["black"] != old.black || o["white"] != old.white ||
            !integral(o["seq"], 0, 1000000) || !o["moves"].isArray() || !o["started"].isBool() ||
            !o["closed"].isBool())
            continue;
        const int seq = o["seq"].toInt();
        if (seq < old.revision)
            continue;
        auto r = old;
        if (!replay(r, o["moves"].toArray()))
            continue;
        if (r.moves.size() < old.moves.size() ||
            !std::equal(old.moves.begin(), old.moves.end(), r.moves.begin()))
            continue;
        r.started = o["started"].toBool();
        r.closed = o["closed"].toBool();
        r.resigned = o["resigned"].toString();
        if ((!r.started && !r.moves.isEmpty()) || (old.started && !r.started) || (old.closed && !r.closed) ||
            (!r.resigned.isEmpty() && (!r.closed || (r.resigned != r.black && r.resigned != r.white))))
            continue;
        if (seq == old.revision && (r.moves != old.moves || r.started != old.started ||
                                    r.closed != old.closed || r.resigned != old.resigned))
            continue;
        if (seq > old.revision) {
            m_error.clear();
            r.revision = seq;
            r.pending = false;
            old = r;
            m_pendingSince.remove(id);
            changedState = true;
        }
    }
    for (const auto& e : events) {
        if (m_seen.contains(e.message.id))
            continue;
        const auto& o = e.object;
        const auto id = o["room"].toString();
        const auto type = o["type"].toString();
        m_seen.insert(e.message.id);
        if (e.message.outgoing) {
            if (e.message.state == SendState::Failed)
                fail("房间消息发送失败，请点击重新同步。");
            continue;
        }
        if (!m_rooms.contains(id))
            continue;
        auto& r = m_rooms[id];
        if (me != r.black || e.message.sender != r.white || o["black"] != r.black || o["white"] != r.white ||
            r.white != e.peer || expired(r) || !m_service->isFriend(r.white))
            continue;
        bool respond = false;
        if (type == "join") {
            if(!r.closed && !r.winner)r.started=true;
            respond=true;
        } else if (type == "move" && r.started && !r.closed && !r.winner && r.moves.size() % 2 == 1 &&
                   integral(o["ply"], 0, 225) && o["ply"].toInt() == r.moves.size() &&
                   integral(o["x"], 0, 14) && integral(o["y"], 0, 14)) {
            auto next = r;
            auto moves = movesJson(r);
            moves.append(QJsonArray{o["x"], o["y"]});
            if (replay(next, moves)) {
                r = next;
                respond = true;
            }
        } else if (type == "leave" && !r.closed && !r.winner) {
            r.closed = true;
            r.resigned = r.white;
            respond = true;
        } else if (type == "sync" && QDateTime::currentMSecsSinceEpoch() - e.message.timestamp < 60000)
            respond = true;
        if (respond) {
            sendState(r);
            changedState = true;
        }
    }
    // Surface transport failures even if the envelope was already consumed.
    for (const auto& c : m_service->conversations())
        if (!c.group)
            for (const auto& m : m_service->messages(c.id))
                if (m.outgoing && m.state == SendState::Failed && !GomokuWire::decode(m.text).isEmpty() &&
                    !m_seen.contains("failed:" + m.id)) {
                    m_seen.insert("failed:" + m.id);
                    const auto envelope = GomokuWire::decode(m.text);
                    const auto id = envelope["room"].toString();
                    if (!m_rooms.contains(id))
                        continue;
                    auto& r = m_rooms[id];
                    const auto type = envelope["type"].toString();
                    if ((type == "state" && envelope["seq"].toInt() < r.revision) ||
                        (type == "move" && envelope["ply"].toInt() < r.moves.size()) ||
                        (type == "join" && r.started) || (type == "leave" && r.closed))
                        continue;
                    r.pending = false;
                    m_pendingSince.remove(id);
                    fail("房间消息发送失败，请检查连接后重新同步。");
                    changedState = true;
                }
    if (changedState)
        emit changed();
}
} // namespace Messager
