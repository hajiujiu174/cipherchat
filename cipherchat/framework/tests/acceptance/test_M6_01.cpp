#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "client_fixture.hpp"
#include <fstream>
#include <map>
#include <set>
#include <windows.h>

using namespace ccfw;
namespace {
const auto project = std::filesystem::path(CCFW_DELIVERY_ROOT);
const auto package = project / "artifacts" / "CipherChat-Windows-x64";
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
std::string hash(const std::filesystem::path& path) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : cc::crypto::sha256(read(path))) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
class PackagedProcess {
    PROCESS_INFORMATION process_{};
public:
    PackagedProcess(const std::filesystem::path& exe, const std::wstring& args, const std::filesystem::path& cwd, bool gui = false) {
        // Keep Windows and normal user variables, but remove all developer tool paths.
        std::map<std::wstring, std::wstring> variables;
        const auto block = GetEnvironmentStringsW();
        for (const wchar_t* entry = block; *entry; entry += wcslen(entry) + 1) {
            const std::wstring item(entry); const auto split = item.find(L'=', 1);
            if (split != std::wstring::npos) {
                auto key = item.substr(0, split); std::transform(key.begin(), key.end(), key.begin(), towupper);
                variables[key] = item.substr(split + 1);
            }
        }
        FreeEnvironmentStringsW(block);
        const auto system = variables.at(L"SYSTEMROOT");
        variables[L"PATH"] = package.wstring() + L";" + system + L"\\System32;" + system;
        for (const auto* key : {L"QT_PLUGIN_PATH", L"QT_QPA_PLATFORM_PLUGIN_PATH", L"QT_QPA_PLATFORM", L"QML2_IMPORT_PATH", L"OPENSSL_CONF", L"OPENSSL_MODULES"}) variables.erase(key);
        std::wstring environment;
        for (const auto& [key, value] : variables) { environment += key + L"=" + value; environment.push_back(0); }
        environment.push_back(0);
        auto command = L"\"" + exe.wstring() + L"\" " + args;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = gui ? SW_SHOWNORMAL : SW_HIDE;
        REQUIRE(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            environment.data(), cwd.c_str(), &startup, &process_));
    }
    ~PackagedProcess() {
        if (running()) { TerminateProcess(process_.hProcess, 77); WaitForSingleObject(process_.hProcess, 5000); }
        CloseHandle(process_.hThread); CloseHandle(process_.hProcess);
    }
    bool running() const { return WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT; }
    DWORD exitCode() const { DWORD code = 0; GetExitCodeProcess(process_.hProcess, &code); return code; }
    HWND window() const {
        struct Search { DWORD process; HWND window = nullptr; } search{process_.dwProcessId};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data); DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
            if (owner == search.process && IsWindowVisible(window)) { search.window = window; return FALSE; } return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.window;
    }
};
std::uint16_t freePort() {
    WSADATA data{}; REQUIRE(WSAStartup(MAKEWORD(2,2), &data) == 0);
    const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); REQUIRE(socket != INVALID_SOCKET);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    int size = sizeof(address); REQUIRE(getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    const auto port = ntohs(address.sin_port); closesocket(socket); WSACleanup(); return port;
}
}

TEST_CASE("M6-01 portable release manifest and rendered document match the delivered artifacts") {
    const auto manifest = nlohmann::json::parse(read(package / "manifest.json"));
    REQUIRE(manifest.at("architecture") == "x64"); REQUIRE(manifest.at("configuration") == "Release");
    REQUIRE(manifest.at("files").size() >= 10);
    std::set<std::string> paths;
    for (const auto& item : manifest.at("files")) {
        const auto relative = std::filesystem::u8path(item.at("path").get<std::string>());
        REQUIRE_FALSE(relative.is_absolute());
        for (const auto& part : relative) REQUIRE(part != "..");
        const auto file = package / relative;
        INFO(file.string());
        REQUIRE(std::filesystem::is_regular_file(file));
        CHECK(std::filesystem::file_size(file) == item.at("bytes").get<std::uintmax_t>());
        CHECK(hash(file) == item.at("sha256").get<std::string>());
        CHECK(paths.insert(relative.generic_string()).second);
    }
    for (const auto* required : {"cipherchat.exe", "cipherchat-server.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms/qwindows.dll", "start-demo.ps1"}) CHECK(paths.count(required) == 1);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(package)) if (entry.is_regular_file()) {
        const auto name = entry.path().lexically_relative(package).generic_string();
        CHECK((name == "manifest.json" || paths.count(name) == 1));
        CHECK(entry.path().extension() != ".db"); CHECK(entry.path().extension() != ".pem"); CHECK(entry.path().extension() != ".part");
    }
    const auto qa = nlohmann::json::parse(read(project / "artifacts" / "document-qa.json"));
    const auto docx = project / std::filesystem::u8path(qa.at("document").get<std::string>());
    CHECK(hash(docx) == qa.at("sha256").get<std::string>());
    CHECK(read(docx).starts_with("PK"));
    CHECK(qa.at("page_count").get<int>() >= 8);
    CHECK(qa.at("reviewed_pages").size() == qa.at("page_count").get<std::size_t>());
    CHECK(qa.at("last_page_has_content").get<bool>());
    CHECK(qa.at("embedded_images").get<int>() >= 3);
    for (const auto* name : {"alice", "bob"}) {
        auto evidence = nlohmann::json::parse(read(project / "artifacts" / "gui" / (std::string(name) + ".json.done")));
        CHECK(evidence.at("identity_confirmed").get<bool>()); CHECK(evidence.at("progress_seen").get<bool>());
        CHECK(evidence.at("max_gui_gap_ms").get<int>() < 1500);
    }
}

TEST_CASE("M6-01 packaged binaries run with only the package and Windows on PATH") {
    const auto directory = test::tempDirectory("M6-01-portable");
    const auto port = freePort();
    PackagedProcess server(package / "cipherchat-server.exe", std::to_wstring(port) + L" server.db cert.pem key.pem", directory);
    REQUIRE(test::eventually([&] { return std::filesystem::exists(directory / "cert.pem") || !server.running(); }, 10000));
    REQUIRE(server.running()); REQUIRE(std::filesystem::file_size(directory / "cert.pem") > 0);
    ConnectionOptions connection{"127.0.0.1", port, directory / "cert.pem", "localhost", 5000};
    // A new signed certificate is generated before listen; wait on a real verified TLS connection.
    REQUIRE(test::eventually([&] { TlsTransport probe; return static_cast<bool>(probe.connect(connection)); }, 10000));
    auto wrongName = connection; wrongName.expectedServerName = "untrusted.invalid";
    TlsTransport rejected; REQUIRE_FALSE(rejected.connect(wrongName));
    std::filesystem::create_directories(directory / "alice");
    std::filesystem::create_directories(directory / "bob");
    test::RealClient alice("package_alice", directory / "alice"), bob("package_bob", directory / "bob");
    alice.registerAndLogin(connection); bob.registerAndLogin(connection); alice.trust(bob); bob.trust(alice);
    const auto sent = alice.chat.sendText({bob.name, "发布包真实 TLS 加密收发验收", 0, {}}); REQUIRE(sent);
    alice.settle(bob, sent.value());
    auto inbox = bob.store.load(); REQUIRE(inbox); REQUIRE(inbox.value().history.size() == 1);
    REQUIRE(inbox.value().history.front().text == "发布包真实 TLS 加密收发验收");
    const auto reply = bob.chat.sendText({alice.name, "portable reply", 0, {}}); REQUIRE(reply); bob.settle(alice, reply.value());
    const auto guiArgs = L"--host 127.0.0.1 --port " + std::to_wstring(port) + L" --ca \"" + (directory / "cert.pem").wstring()
        + L"\" --server-name localhost --username package_preview --data-dir \"" + (directory / "gui").wstring() + L"\"";
    PackagedProcess gui(package / "cipherchat.exe", guiArgs, directory, true);
    REQUIRE(test::eventually([&] { return gui.window() || !gui.running(); }, 10000)); REQUIRE(gui.running());
    const auto window = gui.window(); REQUIRE(window != nullptr);
    DWORD_PTR response = 0; REQUIRE(SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &response) != 0);
    REQUIRE(PostMessageW(window, WM_CLOSE, 0, 0)); REQUIRE(test::eventually([&] { return !gui.running(); }, 6000)); REQUIRE(gui.exitCode() == 0);
    nlohmann::json evidence{{"configuration", "Release"}, {"path_scope", "package;Windows/System32;Windows"},
        {"tls_verified", true}, {"wrong_hostname_rejected", true}, {"two_accounts_authenticated", true}, {"encrypted_roundtrip", true},
        {"native_gui_started", true}, {"native_gui_responsive", true}, {"native_gui_exit_code", gui.exitCode()},
        {"client_sha256", hash(package / "cipherchat.exe")}, {"server_sha256", hash(package / "cipherchat-server.exe")}};
    std::ofstream output(project / "artifacts" / "package-smoke.json"); output << evidence.dump(2); REQUIRE(output.good());
}
