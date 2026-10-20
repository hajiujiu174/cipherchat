#include "localfiles.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace Messager {
LocalFileLocation localFileLocation(const QString& path) {
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!QFileInfo(directory).isDir())
        return {};
    return {QDir::cleanPath(directory), info.isFile() ? info.absoluteFilePath() : QString()};
}

bool revealLocalFile(const QString& path) {
    const auto location = localFileLocation(path);
    if (!location.available())
        return false;
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return false;
    PIDLIST_ABSOLUTE folder = nullptr;
    PIDLIST_ABSOLUTE file = nullptr;
    const auto nativeDirectory = QDir::toNativeSeparators(location.directory);
    const auto nativeFile = QDir::toNativeSeparators(location.selectedFile);
    HRESULT result = SHParseDisplayName(reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()),
                                       nullptr, &folder, 0, nullptr);
    if (SUCCEEDED(result)) {
        if (!location.selectedFile.isEmpty()) {
            result = SHParseDisplayName(reinterpret_cast<LPCWSTR>(nativeFile.utf16()),
                                       nullptr, &file, 0, nullptr);
            if (SUCCEEDED(result)) {
                PCUITEMID_CHILD child = ILFindLastID(file);
                result = SHOpenFolderAndSelectItems(folder, 1, &child, 0);
            }
        } else {
            result = SHOpenFolderAndSelectItems(folder, 0, nullptr, 0);
        }
    }
    CoTaskMemFree(file);
    CoTaskMemFree(folder);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (FAILED(result))
        qWarning("Could not reveal local file (Shell HRESULT 0x%08lx)", static_cast<unsigned long>(result));
    return SUCCEEDED(result);
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(location.directory));
#endif
}
} // namespace Messager
