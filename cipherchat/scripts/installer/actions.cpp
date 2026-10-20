#define UNICODE
#define _UNICODE
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr char marker[] = "CipherChat-MSI-91f73744-5e16-4784-a941-ae0ed794187c\n";
std::wstring property(MSIHANDLE session, const wchar_t* name) {
    DWORD size = 0; wchar_t empty = 0; MsiGetPropertyW(session, name, &empty, &size);
    std::wstring result(size + 1, L'\0'); ++size;
    if (MsiGetPropertyW(session, name, result.data(), &size) != ERROR_SUCCESS) throw 1;
    result.resize(size); return result;
}
void error(MSIHANDLE session, const wchar_t* message) {
    auto record = MsiCreateRecord(1); MsiRecordSetStringW(record, 0, message);
    MsiProcessMessage(session, INSTALLMESSAGE_ERROR, record); MsiCloseHandle(record);
}
bool same(const fs::path& a, const fs::path& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
void noLinks(const fs::path& path) {
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        const auto attr = GetFileAttributesW(current.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) throw 1;
        if (attr == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) throw 1;
        if (current == current.parent_path()) break;
    }
}
fs::path installation(MSIHANDLE session, const wchar_t* field) {
    auto text = property(session, field);
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    fs::path root(text);
    if (!root.is_absolute() || root.has_root_name() == false || text.starts_with(L"\\\\")) throw 1;
    root = root.lexically_normal(); noLinks(root);
    if (!same(root.filename(), L"CipherChat")) throw 1;
    PWSTR local = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local) != S_OK) throw 1;
    const auto data = fs::path(local) / L"CipherChat"; CoTaskMemFree(local);
    if (same(root, data)) throw 1; // Never install over the existing account/profile directory.
    for (auto p = root; p != p.parent_path(); p = p.parent_path()) if (same(p, data)) throw 1;
    return root;
}
}
extern "C" __declspec(dllexport) UINT __stdcall ValidateLocation(MSIHANDLE session) {
    try {
        const auto root = installation(session, L"INSTALLDIR");
        auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) throw 1;
        PROCESSENTRY32W process{}; process.dwSize = sizeof(process); bool busy = false;
        if (Process32FirstW(snapshot, &process)) do {
            if (_wcsicmp(process.szExeFile, L"cipherchat.exe") && _wcsicmp(process.szExeFile, L"cipherchat-updater.exe") && _wcsicmp(process.szExeFile, L"cipherchat-launcher.exe")) continue;
            auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
            if (!handle) continue;
            wchar_t name[32768]; DWORD size = 32768;
            if (QueryFullProcessImageNameW(handle, 0, name, &size)) {
                for (auto p = fs::path(name).parent_path(); p != p.parent_path(); p = p.parent_path()) if (same(p, root)) busy = true;
            }
            CloseHandle(handle);
        } while (Process32NextW(snapshot, &process));
        CloseHandle(snapshot);
        if (busy) { error(session, L"请先退出此安装目录中的 CipherChat 和更新程序，然后重新安装、修复或卸载。"); return ERROR_INSTALL_FAILURE; }
        const auto owner = root / L".cipherchat-msi-owner"; noLinks(owner);
        if (fs::is_regular_file(owner) && fs::file_size(owner) == sizeof(marker) - 1) {
            std::ifstream in(owner, std::ios::binary); const std::string value{std::istreambuf_iterator<char>(in), {}};
            if (value == marker) MsiSetPropertyW(session, L"CAN_CLEAN_UPDATES", L"1");
        }
        return ERROR_SUCCESS;
    }
    catch (...) { error(session, L"安装目录须为本地普通文件夹，末级名称为 CipherChat，且不能位于账号数据目录或链接目录中。"); return ERROR_INSTALL_FAILURE; }
}
extern "C" __declspec(dllexport) UINT __stdcall CleanUpdates(MSIHANDLE session) {
    try {
        const auto root = installation(session, L"CustomActionData");
        // Commit action: ownership was checked before RemoveFiles. MSI has now removed its marker.
        const auto updates = root / L".updates"; noLinks(updates);
        if (!fs::exists(updates)) return ERROR_SUCCESS;
        if (!fs::is_directory(updates)) throw 1;
        // Validate every entry before deletion; never follow junctions outside the owned tree.
        std::vector<fs::path> entries;
        for (const auto& entry : fs::recursive_directory_iterator(updates)) {
            noLinks(entry.path()); entries.push_back(entry.path());
        }
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            noLinks(*it); if (!fs::remove(*it)) throw 1;
        }
        noLinks(updates); fs::remove(updates);
        if (fs::is_empty(root)) fs::remove(root);
        return ERROR_SUCCESS;
    } catch (...) {
        error(session, L"程序已卸载，但更新目录未完全清理；账号和聊天数据已保留。");
        return ERROR_SUCCESS;
    }
}
