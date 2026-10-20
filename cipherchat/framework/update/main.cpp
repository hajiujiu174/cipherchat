#include "update.hpp"
#include "windows_process.hpp"
#include <shlobj.h>
#include <tlhelp32.h>
#include <iostream>
#include <mutex>
namespace fs = std::filesystem;
using namespace ccfw::updates;
namespace {
void emit(const Json& event) { static std::mutex output; std::lock_guard lock(output); std::cout << event.dump() << std::endl; }
struct Lock {
    HANDLE handle;
    explicit Lock(const fs::path& root) {
        auto normalized = fs::weakly_canonical(root).wstring();
        for (auto& c : normalized) c = static_cast<wchar_t>(std::towlower(c));
        const auto hash = digest(std::string(reinterpret_cast<const char*>(normalized.data()), normalized.size() * sizeof(wchar_t)));
        handle = CreateMutexW(nullptr, FALSE, (L"Local\\CipherChatUpdate-" + std::wstring(hash.begin(),hash.end())).c_str());
        if (!handle) throw std::runtime_error("无法创建更新锁");
        auto result = WaitForSingleObject(handle, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) { CloseHandle(handle); throw std::runtime_error("另一个更新任务正在运行"); }
    }
    ~Lock() { ReleaseMutex(handle); CloseHandle(handle); }
};
void waitForClient(DWORD pid) {
    auto handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!handle) { if (GetLastError() == ERROR_INVALID_PARAMETER) return; throw std::runtime_error("无法等待客户端退出"); }
    const auto result = WaitForSingleObject(handle, 120000); CloseHandle(handle);
    if (result != WAIT_OBJECT_0) throw std::runtime_error("等待客户端退出超时，版本未切换");
}
int fastUpdate() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    BROWSEINFOW browse{}; browse.lpszTitle = L"选择原 CipherChat 文件夹（包含 cipherchat-launcher.exe）"; browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    const auto selected = SHBrowseForFolderW(&browse); if (!selected) { CoUninitialize(); return 0; }
    wchar_t path[MAX_PATH]; const auto resolved = SHGetPathFromIDListW(selected,path); CoTaskMemFree(selected); CoUninitialize();
    if (!resolved) throw std::runtime_error("无法读取所选文件夹");
    const auto root = installationRoot(path); const auto current = fs::weakly_canonical(root/versionPath(root));
    if (!fs::is_regular_file(root/"cipherchat-launcher.exe")) throw std::runtime_error("请选择包含 cipherchat-launcher.exe 的原安装文件夹");
    Lock lock(root); RemoteOptions options; options.status = emit; const auto fetch = remoteFetcher(current,options);
    const auto latest = check(current,fetch);
    if (latest.at("sequence") == readJson(current/"release.json").at("sequence")) {
        MessageBoxW(nullptr,L"当前已经是最新版本。",L"CipherChat 快速更新",MB_OK); return 0;
    }
    const auto staged = stage(current,fetch,emit,{downloadConnections}); emit(staged);
    if (MessageBoxW(nullptr,L"下载和解压已完成。\n\n请完全退出原 CipherChat（托盘菜单中选择退出），再点击“确定”完成更新。\n点击“取消”会保留下载，下次可以继续。",L"CipherChat 快速更新",MB_OKCANCEL) != IDOK) return 0;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("无法检查正在运行的客户端");
    std::vector<DWORD> clients; PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot,&entry)) do {
        if (_wcsicmp(entry.szExeFile,L"cipherchat.exe") && _wcsicmp(entry.szExeFile,L"cipherchat-rhine.exe")) continue;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if (process) {
            wchar_t executable[32768]; DWORD size = 32768;
            std::error_code error;
            if (QueryFullProcessImageNameW(process,0,executable,&size) && fs::equivalent(fs::path(executable).parent_path(),current,error)) clients.push_back(entry.th32ProcessID);
            CloseHandle(process);
        }
    } while (Process32NextW(snapshot,&entry));
    CloseHandle(snapshot); for (auto pid : clients) waitForClient(pid);
    const auto ver = staged.at("version").get<std::string>(); activate(root,ver);
    const Json result{{"event","activated"},{"version",ver}}; writeAtomic(root/".updates"/"last-result.json",result.dump()); emit(result);
    spawn(root/"cipherchat-launcher.exe",{}); return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    fs::path root; bool switching = false; bool switched = false;
    try {
        if (argc == 1) return fastUpdate();
        if (argc < 3) throw std::runtime_error("用法: --check/--stage <当前版本目录> 或 --activate <安装目录> <版本> 或 --rollback <安装目录>");
        const std::wstring mode = argv[1]; const auto dir = fs::absolute(argv[2]); root = installationRoot(dir);
        if (mode == L"--check") {
            const auto latest = check(dir,remoteFetcher(dir)); const auto local = readJson(dir / "release.json");
            emit({{"event","checked"},{"current",local.at("version")},{"version",latest.at("version")},{"available",latest.at("sequence") != local.at("sequence")},{"notes",latest.value("notes",std::string{})}}); return 0;
        }
        Lock lock(root);
        if (mode == L"--stage" || mode == L"--stage-blocks") { RemoteOptions options; options.status = emit; emit(stage(dir,remoteFetcher(dir,options),emit,{downloadConnections,mode == L"--stage"})); return 0; }
        if (mode != L"--activate" && mode != L"--rollback") throw std::runtime_error("未知更新操作");
        switching = true; std::string ver; int start = 3;
        if (mode == L"--activate") { if (argc < 4) throw std::runtime_error("缺少版本号"); ver = fs::path(argv[3]).string(); start = 4; }
        DWORD pid = 0;
        for (int i = start; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--wait-pid" && i + 1 < argc) pid = std::stoul(argv[++i]);
            else throw std::runtime_error("未知启动参数");
        }
        std::vector<std::wstring> args;
        if (pid) {
            auto restart = readJson(root / ".updates" / ("restart-" + std::to_string(pid) + ".json"));
            for (const auto& arg : restart) args.push_back(fs::u8path(arg.get<std::string>()).wstring());
            waitForClient(pid);
        }
        if (mode == L"--activate") activate(root,ver); else rollback(root);
        switched = true;
        const auto active = root / versionPath(root);
        for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == L"--ca") args[i + 1] = (active / "server-cert.pem").wstring();
        spawn(root / "cipherchat-launcher.exe", args);
        Json result{{"event","activated"},{"version",readJson(active / "release.json").at("version")}};
        writeAtomic(root / ".updates" / "last-result.json", result.dump()); emit(result); return 0;
    } catch (const std::exception& e) {
        Json result{{"event","error"},{"message",e.what()},{"switched",switched}}; emit(result);
        if (argc == 1) MessageBoxW(nullptr,L"本次更新未完成，下载内容已保留。请检查所选文件夹或网络后重试。",L"CipherChat 快速更新",MB_OK | MB_ICONERROR);
        if (switching && !root.empty()) {
            try { writeAtomic(root / ".updates" / "last-result.json",result.dump()); } catch (...) {}
            MessageBoxW(nullptr, switched ? L"版本已切换，但自动启动失败。请通过启动入口重试，或运行“回退更新.cmd”。" : L"更新未完成，原版本仍可使用。请通过启动入口重试，详情见软件更新窗口。", L"CipherChat 更新", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
