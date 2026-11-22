#pragma once
#include <QString>

namespace Messager {
struct LocalFileLocation {
    QString directory;
    QString selectedFile;
    // 返回文件传输的可用性。
    bool available() const { return !directory.isEmpty(); }
};
// 返回文件传输使用的本机文件位置。
LocalFileLocation localFileLocation(const QString& path);
// 显示位置本机文件。
bool revealLocalFile(const QString& path);
} // namespace Messager
