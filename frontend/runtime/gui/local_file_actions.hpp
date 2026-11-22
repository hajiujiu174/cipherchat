#pragma once
#include <QString>
namespace ccfw::gui {
// 打开本机文件。
bool openLocalFile(const QString& file);
// 显示位置本机文件。
bool revealLocalFile(const QString& file);
}
