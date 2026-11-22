#include "local_file_actions.hpp"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif
namespace ccfw::gui {
bool openLocalFile(const QString& file) {
    const QFileInfo info(file);
    return info.isFile() && QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}
bool revealLocalFile(const QString& file) {
    const QFileInfo info(file);
    if (!info.isFile()) return false;
#ifdef _WIN32
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    PIDLIST_ABSOLUTE item = nullptr;
    const auto native = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const HRESULT parsed = SHParseDisplayName(native.c_str(), nullptr, &item, 0, nullptr);
    const bool opened = SUCCEEDED(parsed) && SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    if (item) CoTaskMemFree(item);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return opened;
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}
}
