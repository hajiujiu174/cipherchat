#pragma once

#include <QJsonArray>
#include <QStringList>

namespace Messager::NextalkJson {
// 将 JSON 数组中的文本项转换为 Qt 字符串列表。
inline QStringList strings(const QJsonValue& v) {
    QStringList r;
    for (const auto& x : v.toArray())
        r << x.toString();
    return r;
}
}
