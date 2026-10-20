#define UNICODE
#define _UNICODE
#include <windows.h>
#include <msi.h>
#include <bcrypt.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <string>
#include "payload_hash.h"
namespace fs = std::filesystem;
static std::string sha256(const BYTE* data, DWORD bytes) {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{}; BYTE digest[32]{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw 1;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(alg, 0); throw 1; }
    auto status = BCryptHashData(hash, const_cast<BYTE*>(data), bytes, 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest, 32, 0);
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0); if (status < 0) throw 1;
    std::string out; for (BYTE b : digest) { out += "0123456789abcdef"[b >> 4]; out += "0123456789abcdef"[b & 15]; } return out;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    fs::path temporary;
    try {
        auto resource = FindResourceW(instance, MAKEINTRESOURCEW(101), RT_RCDATA);
        auto loaded = LoadResource(instance, resource);
        auto data = static_cast<const BYTE*>(LockResource(loaded)); DWORD size = SizeofResource(instance, resource);
        if (!resource || !loaded || !data || sha256(data, size) != PAYLOAD_SHA256) throw 1;
        wchar_t base[32768]; if (!GetTempPathW(32768, base)) throw 1;
        wchar_t leaf[80]; GUID id{}; if (CoCreateGuid(&id) != S_OK) throw 1;
        StringFromGUID2(id, leaf, 80); temporary = fs::path(base) / (std::wstring(L"CipherChat-Setup-") + leaf);
        if (!fs::create_directory(temporary)) throw 1;
        const auto package = temporary / L"CipherChat.msi";
        { std::ofstream file(package, std::ios::binary); file.write(reinterpret_cast<const char*>(data), size); if (!file) throw 1; }
        // Only a read-only payload extraction mode is exposed for independent verification.
        if (std::wstring(command) == L"--verify-payload") {
            MessageBoxW(nullptr, (L"安装包完整性校验通过。\n内置 MSI：" + package.wstring()).c_str(), L"CipherChat 安装器", MB_OK);
        } else {
            if (*command) { MessageBoxW(nullptr, L"请直接双击 setup.exe 打开安装向导。", L"CipherChat 安装器", MB_OK | MB_ICONINFORMATION); fs::remove(package); fs::remove(temporary); return ERROR_INVALID_PARAMETER; }
            MsiSetInternalUI(INSTALLUILEVEL_FULL, nullptr);
            UINT result = MsiInstallProductW(package.c_str(), L"REBOOT=ReallySuppress");
            fs::remove(package); fs::remove(temporary);
            if (result != ERROR_SUCCESS && result != ERROR_INSTALL_USEREXIT && result != ERROR_SUCCESS_REBOOT_REQUIRED)
                MessageBoxW(nullptr, (L"安装未完成，Windows Installer 错误码：" + std::to_wstring(result) + L"。\n请先关闭 CipherChat，并检查磁盘空间后重试。").c_str(), L"CipherChat 安装器", MB_OK | MB_ICONERROR);
            return static_cast<int>(result);
        }
        fs::remove(package); fs::remove(temporary); return 0;
    } catch (...) {
        MessageBoxW(nullptr, L"无法准备安装文件。请检查磁盘空间，或重新获取完整的 setup.exe。", L"CipherChat 安装器", MB_OK | MB_ICONERROR);
        return ERROR_INSTALL_FAILURE;
    }
}
