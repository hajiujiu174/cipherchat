#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace ccfw::updates {
// 为 Windows 命令行参数处理引号和反斜杠。
inline std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\""; unsigned slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L"\"";
}
// 启动进程软件更新。
inline void spawn(const std::filesystem::path& exe, const std::vector<std::wstring>& args) {
    std::wstring command = quote(exe.wstring()); for (const auto& arg : args) command += L" " + quote(arg);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process)) throw std::runtime_error("无法启动程序");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
// 返回软件更新使用的executable目录。
inline std::filesystem::path executableDirectory() {
    std::wstring name(32768, L'\0'); const auto length = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (!length || length == name.size()) throw std::runtime_error("无法确定程序路径");
    name.resize(length); return std::filesystem::path(name).parent_path();
}
}
