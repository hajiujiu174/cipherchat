#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace Messager::GomokuWire {
inline QString marker() {
    return QStringLiteral("[[CipherChat:Gomoku:1]]");
}
inline QJsonObject decode(const QString& text) {
    if (text.size() > 8192)
        return {};
    const int at = text.indexOf(marker());
    if (at < 0 || (at > 0 && !text.startsWith("五子棋邀请\n")))
        return {};
    const auto document = QJsonDocument::fromJson(text.mid(at + marker().size()).toUtf8());
    if (!document.isObject())
        return {};
    const auto object = document.object();
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
        header = "五子棋邀请\n" + object["black"].toString() + " 邀请你对弈，点击加入房间。\n";
    return header + marker() + QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
} // namespace Messager::GomokuWire
