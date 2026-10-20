#include "ccfw/local_store.hpp"
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif
namespace ccfw {
namespace {
constexpr cc::BytesView magic = "CCDPAPI1";
constexpr cc::BytesView entropyText = "CipherChat/local-snapshot/DPAPI/v1";
Result<cc::Bytes> failure() {
    return Result<cc::Bytes>::failure({ErrorCode::StorageFailure, "M3-04", "无法保护或解封本地数据"});
}
#ifdef _WIN32
struct ProtectedBlob {
    DATA_BLOB data{};
    ~ProtectedBlob() {
        if (data.pbData) { SecureZeroMemory(data.pbData, data.cbData); LocalFree(data.pbData); }
    }
};
DATA_BLOB view(cc::BytesView data) {
    return {static_cast<DWORD>(data.size()), reinterpret_cast<BYTE*>(const_cast<char*>(data.data()))};
}
#endif
}
Result<cc::Bytes> PlatformSecretProtector::seal(cc::BytesView plain) {
#ifdef _WIN32
    if (plain.size() > 64 * 1024 * 1024) return failure();
    auto input = view(plain);
    auto entropy = view(entropyText);
    ProtectedBlob encrypted;
    if (!CryptProtectData(&input, L"CipherChat", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted.data)) return failure();
    cc::Bytes output(magic);
    for (int shift = 24; shift >= 0; shift -= 8) output.push_back(static_cast<char>(encrypted.data.cbData >> shift));
    output.append(reinterpret_cast<char*>(encrypted.data.pbData), encrypted.data.cbData);
    return Result<cc::Bytes>::success(std::move(output));
#else
    (void)plain;
    return Result<cc::Bytes>::failure({ErrorCode::StorageFailure, "M3-04", "本平台未配置本地秘密保护器"});
#endif
}
Result<cc::Bytes> PlatformSecretProtector::unseal(cc::BytesView protectedData) {
#ifdef _WIN32
    if (protectedData.size() < 12 || !protectedData.starts_with(magic)) return failure();
    std::uint32_t length = 0;
    for (std::size_t i = 8; i < 12; ++i) length = (length << 8) | static_cast<unsigned char>(protectedData[i]);
    if (length > 65 * 1024 * 1024 || length != protectedData.size() - 12) return failure();
    auto input = view(protectedData.substr(12));
    auto entropy = view(entropyText);
    ProtectedBlob plain;
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.data)) return failure();
    return Result<cc::Bytes>::success(cc::Bytes(reinterpret_cast<char*>(plain.data.pbData), plain.data.cbData));
#else
    (void)protectedData;
    return Result<cc::Bytes>::failure({ErrorCode::StorageFailure, "M3-04", "本平台未配置本地秘密保护器"});
#endif
}
}
