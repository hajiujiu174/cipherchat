#pragma once
#include <QString>

namespace Messager {
struct LocalFileLocation {
    QString directory;
    QString selectedFile;
    bool available() const { return !directory.isEmpty(); }
};
LocalFileLocation localFileLocation(const QString& path);
bool revealLocalFile(const QString& path);
} // namespace Messager
