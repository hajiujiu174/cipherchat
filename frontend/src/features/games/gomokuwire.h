#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace Messager::GomokuWire {
// 提供区分游戏控制消息和普通聊天文字的协议标识。
inline QString marker() {
    return QStringLiteral("[[NEXTALK:Gomoku:1]]");
}
// 返回游戏联机使用的游戏类型 ID。
inline QString gameId(const QJsonObject& o) { return o["game"].toString("gomoku"); }
// 返回游戏联机使用的标题。
inline QString title(const QString& game) {
    return game=="chess"?QStringLiteral("国际象棋"):game=="xiangqi"?QStringLiteral("中国象棋"):game == "badminton" ? QStringLiteral("火柴人羽毛球") : game == "tanktrouble" ? QStringLiteral("坦克动荡") : game == "connectfour" ? QStringLiteral("四子棋") : game == "reversi" ? QStringLiteral("黑白棋") : QStringLiteral("五子棋");
}
// 返回游戏联机使用的游戏Marker。
inline QString gameMarker() { return QStringLiteral("[[NEXTALK:BoardGame:1]]"); }
// 解码游戏联机。
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
    if ((legacy && game != "gomoku") || (!legacy && !QStringList{"connectfour","reversi","tanktrouble","badminton","chess","xiangqi"}.contains(game)) ||
        (at > 0 && (object["type"] != "invite" || !text.startsWith(title(game) + "邀请\n")))) return {};
    if((game=="tanktrouble"||game=="badminton") && object["type"]!="invite")return {};
    static const QRegularExpression room("^[0-9a-f]{32}$");
    if (object["v"].toInt() != 1 || !room.match(object["room"].toString()).hasMatch() ||
        !QStringList{"invite", "join", "move", "state", "leave", "sync"}.contains(object["type"].toString()))
        return {};
    return object;
}
// 判断控制条件是否成立。
inline bool isControl(const QString& text) {
    const auto o = decode(text);
    return !o.isEmpty() && o["type"] != "invite";
}
// 编码游戏联机。
inline QString encode(QJsonObject object) {
    object["v"] = 1;
    QString header;
    if (object["type"] == "invite")
        header = title(gameId(object)) + "邀请\n" + object["black"].toString() + (gameId(object)=="badminton" ? " 邀请你进行羽毛球对战，点击加入房间。\n" : gameId(object)=="tanktrouble" ? " 邀请你进入迷宫坦克对战，点击加入房间。\n" : " 邀请你对弈，点击加入房间。\n");
    return header + (gameId(object) == "gomoku" ? marker() : gameMarker()) + QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
} // namespace Messager::GomokuWire
