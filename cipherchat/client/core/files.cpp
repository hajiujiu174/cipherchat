#include "client/core/files.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace cc::files {

namespace {
constexpr BytesView kFileKeyInfo = "CipherChat/FileChunk/v1";
}  // namespace

Bytes chunkNonce(const Bytes& fileKey, std::uint32_t seq) {
    const unsigned char s[4] = {static_cast<unsigned char>(seq >> 24),
                                static_cast<unsigned char>(seq >> 16),
                                static_cast<unsigned char>(seq >> 8),
                                static_cast<unsigned char>(seq)};
    const Bytes info(kFileKeyInfo) ;
    Bytes ikm(info);
    ikm.append(reinterpret_cast<const char*>(s), 4);
    return cc::crypto::hkdfSha256(fileKey, "", ikm, 12);
}

ChunkSeal sealChunk(const Bytes& fileKey, BytesView fileId,
                    std::uint32_t seq, BytesView prevTag, BytesView plain) {
    const Bytes nonce = chunkNonce(fileKey, seq);
    Bytes aad(fileId);
    const unsigned char s[4] = {static_cast<unsigned char>(seq >> 24),
                                static_cast<unsigned char>(seq >> 16),
                                static_cast<unsigned char>(seq >> 8),
                                static_cast<unsigned char>(seq)};
    aad.append(reinterpret_cast<const char*>(s), 4);
    aad.append(prevTag);
    ChunkSeal out;
    const Bytes sealed = cc::crypto::aesGcmEncrypt(fileKey, nonce, plain, aad);
    // aesGcmEncrypt 返回 密文||标签；此处拆开，标签作为链式 AAD 单独传递
    out.tag.assign(sealed, sealed.size() - cc::crypto::kTagLen,
                   cc::crypto::kTagLen);
    out.cipher.assign(sealed, 0, sealed.size() - cc::crypto::kTagLen);
    return out;
}

std::optional<Bytes> openChunk(const Bytes& fileKey, BytesView fileId,
                               std::uint32_t seq, BytesView prevTag,
                               BytesView cipher, BytesView tag) {
    if (tag.size() != cc::crypto::kTagLen || fileKey.size() != 32 ||
        (!prevTag.empty() && prevTag.size() != cc::crypto::kTagLen)) return std::nullopt;
    const Bytes nonce = chunkNonce(fileKey, seq);
    Bytes aad(fileId);
    const unsigned char s[4] = {static_cast<unsigned char>(seq >> 24),
                                static_cast<unsigned char>(seq >> 16),
                                static_cast<unsigned char>(seq >> 8),
                                static_cast<unsigned char>(seq)};
    aad.append(reinterpret_cast<const char*>(s), 4);
    aad.append(prevTag);
    // 密文与标签分离存放时重新拼接
    Bytes sealed(cipher);
    sealed.append(tag);
    return cc::crypto::aesGcmDecrypt(fileKey, nonce, sealed, aad);
}

bool readFile(const std::string& path, Bytes& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    f.read(out.data(), size);
    return f.good() || f.eof();
}

bool writeFile(const std::string& path, const Bytes& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return f.good();
}

}  // namespace cc::files
