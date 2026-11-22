#include "ccfw/local_store.hpp"
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include "common/crypto/crypto.hpp"
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#endif
namespace ccfw {
namespace {
constexpr cc::BytesView magic = "CCDPAPI1";
constexpr cc::BytesView entropyText = "NEXTALK/local-snapshot/DPAPI/v1";
// Historical snapshots retain their original cryptographic domain on read.
constexpr cc::BytesView legacyEntropyText = "Cipher" "Chat/local-snapshot/DPAPI/v1";
// 返回secret_protector 模块使用的失败结果。
Result<cc::Bytes> failure() {
    return Result<cc::Bytes>::failure({ErrorCode::StorageFailure, "M3-04", "无法保护或解封本地数据"});
}
#ifdef _WIN32
struct ProtectedBlob {
    DATA_BLOB data{};
    // 销毁secret_protector 模块时释放它持有的资源。
    ~ProtectedBlob() {
        if (data.pbData) { SecureZeroMemory(data.pbData, data.cbData); LocalFree(data.pbData); }
    }
};
// 返回secret_protector 模块使用的视图。
DATA_BLOB view(cc::BytesView data) {
    return {static_cast<DWORD>(data.size()), reinterpret_cast<BYTE*>(const_cast<char*>(data.data()))};
}
#endif
}
#ifndef _WIN32
namespace {
// 返回secret_protector 模块使用的platform键。
Result<cc::Bytes> platformKey() {
    try {
        const auto* base=std::getenv("XDG_DATA_HOME");std::filesystem::path directory;
        if(base&&*base)directory=std::filesystem::path(base)/"NEXTALK";
        else {const auto* home=std::getenv("HOME");if(!home||!*home)return failure();
#ifdef __APPLE__
            directory=std::filesystem::path(home)/"Library/Application Support/NEXTALK";
#else
            directory=std::filesystem::path(home)/".local/share/NEXTALK";
#endif
        }
        std::filesystem::create_directories(directory);
        struct stat status{};if(lstat(directory.c_str(),&status)||!S_ISDIR(status.st_mode)||status.st_uid!=getuid()||chmod(directory.c_str(),0700))return failure();
        const auto path=directory/"snapshot-master.key";
        const int fd=open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return failure();
        struct Handle {int fd;/* 销毁secret_protector 模块时释放它持有的资源。 */ ~Handle(){flock(fd,LOCK_UN);close(fd);}} handle{fd};
        if(flock(fd,LOCK_EX)||fstat(fd,&status)||!S_ISREG(status.st_mode)||status.st_uid!=getuid()||status.st_nlink!=1||(status.st_mode&0077))return failure();
        cc::Bytes key(32,'\0');
        if(!status.st_size){key=cc::crypto::randomBytes(32);if(write(fd,key.data(),key.size())!=ssize_t(key.size())||fsync(fd))return failure();}
        else if(status.st_size!=32||read(fd,key.data(),key.size())!=ssize_t(key.size()))return failure();
        return Result<cc::Bytes>::success(std::move(key));
    }catch(...){return failure();}
}
}
#endif
// 保护secret_protector 模块。
Result<cc::Bytes> PlatformSecretProtector::seal(cc::BytesView plain) {
#ifdef _WIN32
    if (plain.size() > 64 * 1024 * 1024) return failure();
    auto input = view(plain);
    auto entropy = view(entropyText);
    ProtectedBlob encrypted;
    if (!CryptProtectData(&input, L"NEXTALK", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted.data)) return failure();
    cc::Bytes output(magic);
    for (int shift = 24; shift >= 0; shift -= 8) output.push_back(static_cast<char>(encrypted.data.cbData >> shift));
    output.append(reinterpret_cast<char*>(encrypted.data.pbData), encrypted.data.cbData);
    return Result<cc::Bytes>::success(std::move(output));
#else
    if(plain.size()>64*1024*1024)return failure();
    auto key=platformKey();if(!key)return key;
    try {const auto nonce=cc::crypto::randomBytes(12);auto cipher=cc::crypto::aesGcmEncrypt(key.value(),nonce,plain,"NEXTALK/POSIX/snapshot/v1");std::fill(key.value().begin(),key.value().end(),0);return Result<cc::Bytes>::success("NTPOSIX1"+nonce+cipher);}catch(...){return failure();}
#endif
}
// 解除保护secret_protector 模块。
Result<cc::Bytes> PlatformSecretProtector::unseal(cc::BytesView protectedData) {
#ifdef _WIN32
    if (protectedData.size() < 12 || !protectedData.starts_with(magic)) return failure();
    std::uint32_t length = 0;
    for (std::size_t i = 8; i < 12; ++i) length = (length << 8) | static_cast<unsigned char>(protectedData[i]);
    if (length > 65 * 1024 * 1024 || length != protectedData.size() - 12) return failure();
    auto input = view(protectedData.substr(12));
    auto entropy = view(entropyText);
    ProtectedBlob plain;
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.data)) {
        auto legacyEntropy = view(legacyEntropyText);
        ProtectedBlob legacyPlain;
        if (!CryptUnprotectData(&input, nullptr, &legacyEntropy, nullptr, nullptr,
                                CRYPTPROTECT_UI_FORBIDDEN, &legacyPlain.data)) return failure();
        return Result<cc::Bytes>::success(cc::Bytes(reinterpret_cast<char*>(legacyPlain.data.pbData), legacyPlain.data.cbData));
    }
    return Result<cc::Bytes>::success(cc::Bytes(reinterpret_cast<char*>(plain.data.pbData), plain.data.cbData));
#else
    if(protectedData.size()<36||protectedData.size()>65*1024*1024||protectedData.substr(0,8)!="NTPOSIX1")return failure();
    auto key=platformKey();if(!key)return key;
    try {const auto plain=cc::crypto::aesGcmDecrypt(key.value(),protectedData.substr(8,12),protectedData.substr(20),"NEXTALK/POSIX/snapshot/v1");std::fill(key.value().begin(),key.value().end(),0);if(!plain)return failure();return Result<cc::Bytes>::success(*plain);}catch(...){return failure();}
#endif
}
}
