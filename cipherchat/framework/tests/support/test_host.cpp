#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace {
std::string utf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3 || std::wcscmp(argv[1], L"--module")) {
        std::fputs("Usage: cipherchat-tests --module <absolute test DLL> [test arguments]\n", stderr);
        return 2;
    }
    // Qt reads the CRT environment; publish before module initialization too.
    if (_wputenv_s(L"CCFW_TEST_MODULE", argv[2])) return 5;
    const auto module = LoadLibraryExW(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        std::fprintf(stderr, "Cannot load test module (Windows error %lu).\n", GetLastError());
        return 3;
    }
    const auto entry = reinterpret_cast<int (*)(int, char**)>(GetProcAddress(module, "ccfw_run_test"));
    if (!entry) {
        std::fputs("The selected module has no test entry point.\n", stderr);
        return 4;
    }
    std::vector<std::string> arguments{utf8(argv[0])};
    for (int i = 3; i < argc; ++i) arguments.push_back(utf8(argv[i]));
    std::vector<char*> raw;
    for (auto& argument : arguments) raw.push_back(argument.data());
    raw.push_back(nullptr);
    // Keep the module loaded until process exit, after Qt and test teardown.
    return entry(static_cast<int>(arguments.size()), raw.data());
}
