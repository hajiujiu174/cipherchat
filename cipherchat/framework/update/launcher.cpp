#include "windows_process.hpp"
#include <shellapi.h>
#include <fstream>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        const auto root = ccfw::updates::executableDirectory(); std::string pointer = ".";
        if (std::filesystem::exists(root / "current.txt")) {
            if (std::filesystem::file_size(root / "current.txt") > 100) throw std::runtime_error("pointer");
            std::ifstream input(root / "current.txt", std::ios::binary); pointer.assign(std::istreambuf_iterator<char>(input), {});
        }
        const auto ver = pointer.size() > 18 ? pointer.substr(18) : std::string{};
        if (pointer != "." && (!pointer.starts_with(".updates/versions/") || ver.empty() || ver.size() > 48 || ver.front() == '.' || ver.back() == '.' || ver.find("..") != std::string::npos || ver.find_first_not_of("0123456789.-") != std::string::npos)) throw std::runtime_error("pointer");
        const auto dir = root / pointer;
        for (auto p = dir; p != root && !p.empty(); p = p.parent_path()) {
            auto attr = GetFileAttributesW(p.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("directory");
        }
        int count = 0; auto raw = CommandLineToArgvW(GetCommandLineW(), &count); if (!raw) throw std::runtime_error("arguments");
        std::vector<std::wstring> args; for (int i = 1; i < count; ++i) args.emplace_back(raw[i]); LocalFree(raw);
#ifdef CCFW_MESSAGER_LAUNCHER
        ccfw::updates::spawn(dir / "Messager.exe", args); return 0;
#else
        ccfw::updates::spawn(dir / "cipherchat.exe", args); return 0;
#endif
    } catch (...) {
        MessageBoxW(nullptr, L"无法启动当前版本。请检查安装目录，或运行“回退更新.cmd”。", L"CipherChat", MB_OK | MB_ICONERROR); return 1;
    }
}
