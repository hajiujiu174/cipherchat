#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace Messager::GomokuWire {
inline QString marker() {
    return QStringLiteral("[[CipherChat:Gomoku:1]]");
}
inline QString gameId(const QJsonObject& o) { return o["game"].toString("gomoku"); }
inline QString title(const QString& game) {
    return game == "connectfour" ? QStringLiteral("四子棋") : game == "reversi" ? QStringLiteral("黑白棋") : QStringLiteral("五子棋");
}
inline QString gameMarker() { return QStringLiteral("[[CipherChat:BoardGame:1]]"); }
inline QJsonObject decode(const QString& text) {
    if (text.size() > 8192)
        return {};
    const bool legacy = text.contains(marker());
    const auto tag = legacy ? marker() : gameMarker();
    const int at = text.indexOf(tag);
    if (at < 0)
        return {};
    const auto document = QJsonDocument::fromJson(text.mid(at + tag.size()).toUtf8());
    if (!document.isObject())
        return {};
    const auto object = document.object();
    const auto game = gameId(object);
    if ((legacy && game != "gomoku") || (!legacy && game != "connectfour" && game != "reversi") ||
        (at > 0 && (object["type"] != "invite" || !text.startsWith(title(game) + "邀请\n")))) return {};
    static const QRegularExpression room("^[0-9a-f]{32}$");
    if (object["v"].toInt() != 1 || !room.match(object["room"].toString()).hasMatch() ||
        !QStringList{"invite", "join", "move", "state", "leave", "sync"}.contains(object["type"].toString()))
        return {};
    return object;
}
inline bool isControl(const QString& text) {
    const auto o = decode(text);
    return !o.isEmpty() && o["type"] != "invite";
}
inline QString encode(QJsonObject object) {
    object["v"] = 1;
    QString header;
    if (object["type"] == "invite")
        header = title(gameId(object)) + "邀请\n" + object["black"].toString() + " 邀请你对弈，点击加入房间。\n";
    return header + (gameId(object) == "gomoku" ? marker() : gameMarker()) + QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
} // namespace Messager::GomokuWire
