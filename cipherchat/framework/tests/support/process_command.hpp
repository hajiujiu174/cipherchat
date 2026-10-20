#pragma once
#include <filesystem>
#include <string>

namespace ccfw::test {
struct ProcessCommand {
    std::filesystem::path executable;
    std::wstring arguments;
};
inline ProcessCommand processCommand(const std::filesystem::path& module, const std::filesystem::path& config) {
#ifdef CCFW_TEST_HOST_PATH
    const std::filesystem::path host(CCFW_TEST_HOST_PATH);
    return {host, L"\"" + host.wstring() + L"\" --module \"" + module.wstring() + L"\" \"" + config.wstring() + L"\""};
#else
    return {module, L"\"" + module.wstring() + L"\" \"" + config.wstring() + L"\""};
#endif
}
}
