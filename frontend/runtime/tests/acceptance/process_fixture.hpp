#pragma once
#include "client_fixture.hpp"
#include "../support/process_command.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
namespace ccfw::test {
class Process {
#ifdef _WIN32
    PROCESS_INFORMATION process_{};
#endif
public:
    // 构造测试，初始化后续操作需要的状态或依赖。
    explicit Process(const std::filesystem::path& config, bool visible = false) {
#ifdef _WIN32
        auto command = processCommand(std::filesystem::path(CCFW_PROCESS_CLIENT_PATH), config);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = visible ? SW_SHOWNORMAL : SW_HIDE;
        REQUIRE(CreateProcessW(command.executable.c_str(), command.arguments.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_));
#else
        throw std::runtime_error("DPAPI recovery acceptance requires Windows");
#endif
    }
    // 销毁测试时释放它持有的资源。
    ~Process() {
#ifdef _WIN32
        crash(); CloseHandle(process_.hThread); CloseHandle(process_.hProcess);
#endif
    }
    // 验证“crash”场景中的关键结果，断言失败时由测试框架报告。
    void crash() {
#ifdef _WIN32
        if (WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT) {
            REQUIRE(TerminateProcess(process_.hProcess, 77)); REQUIRE(WaitForSingleObject(process_.hProcess, 5000) == WAIT_OBJECT_0);
        }
#endif
    }
    // 获取测试对应的 running，供调用方读取或参与计算。
    bool running() const {
#ifdef _WIN32
        return WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT;
#else
        return false;
#endif
    }
    // 返回测试使用的exit验证码。
    int exitCode() const {
#ifdef _WIN32
        DWORD code = 0; GetExitCodeProcess(process_.hProcess, &code); return static_cast<int>(code);
#else
        return -1;
#endif
    }
};
// 写入配置。
inline void writeConfig(const std::filesystem::path& path, const nlohmann::json& config) { std::ofstream out(path); out << config.dump(2); }
// 验证“loadDisk”场景中的关键结果，断言失败时由测试框架报告。
inline ClientSnapshot loadDisk(const std::filesystem::path& directory, const std::string& name) {
    PlatformSecretProtector protector; SqliteLocalStore store(protector);
    REQUIRE(store.open(directory / "client.db", {"real-test-server", name})); auto loaded = store.load(); REQUIRE(loaded); return loaded.value();
}
}
