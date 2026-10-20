#pragma once
#include "../../src/feature_codec.hpp"
#include "ccfw/result.hpp"
#include "../support/process_command.hpp"
#include <fstream>
#include <thread>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ccfw::compat {
inline nlohmann::json read(const std::filesystem::path& path) {
    try {
#ifdef _WIN32
        HANDLE input = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input == INVALID_HANDLE_VALUE) return nlohmann::json::object();
        LARGE_INTEGER size{}; if (!GetFileSizeEx(input, &size) || size.QuadPart > 1024 * 1024) { CloseHandle(input); return nlohmann::json::object(); }
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0'); DWORD received = 0;
        const bool ok = ReadFile(input, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr); CloseHandle(input);
        if (!ok || received != bytes.size()) return nlohmann::json::object();
        auto j = nlohmann::json::parse(bytes);
#else
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 1024 * 1024) return nlohmann::json::object();
        std::ifstream in(path); nlohmann::json j; in >> j;
#endif
        return j.is_object() ? j : nlohmann::json::object();
    } catch (...) { return nlohmann::json::object(); }
}
inline void write(const std::filesystem::path& path, const nlohmann::json& j) {
    const auto temp = std::filesystem::path(path.wstring() + L".tmp"); { std::ofstream out(temp, std::ios::binary); if (!out || !(out << j.dump(2))) throw std::runtime_error("兼容测试状态无法保存"); }
#ifdef _WIN32
    for (unsigned attempt = 0;; ++attempt) {
        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        const DWORD error = GetLastError();
        if (attempt >= 50 || (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED)) throw std::runtime_error("兼容测试状态无法提交，系统错误 " + std::to_string(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    std::filesystem::rename(temp, path);
#endif
}
class Process {
#ifdef _WIN32
    PROCESS_INFORMATION handles_{};
#endif
public:
    Process(const std::filesystem::path& exe, const std::filesystem::path& argument) {
#ifdef _WIN32
        auto command = ccfw::test::processCommand(exe, argument);
        STARTUPINFOW info{}; info.cb = sizeof(info); info.dwFlags = STARTF_USESHOWWINDOW; info.wShowWindow = SW_HIDE;
        if (!CreateProcessW(command.executable.c_str(), command.arguments.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, exe.parent_path().c_str(), &info, &handles_)) throw std::runtime_error("兼容测试子进程启动失败");
#else
        (void)exe; (void)argument; throw std::runtime_error("当前兼容测试启动器需要 Windows");
#endif
    }
    ~Process() { stop();
#ifdef _WIN32
        CloseHandle(handles_.hThread); CloseHandle(handles_.hProcess);
#endif
    }
    bool running() const {
#ifdef _WIN32
        return WaitForSingleObject(handles_.hProcess, 0) == WAIT_TIMEOUT;
#else
        return false;
#endif
    }
    void stop() noexcept {
#ifdef _WIN32
        if (running()) { TerminateProcess(handles_.hProcess, 77); WaitForSingleObject(handles_.hProcess, 3000); }
#endif
    }
    unsigned long pid() const {
#ifdef _WIN32
        return handles_.dwProcessId;
#else
        return 0;
#endif
    }
};

}
