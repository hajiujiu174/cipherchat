#pragma once
// 文件传输的纯加密原语（可独立单测）：
//   文件密钥 32B 随机，经棘轮会话以 file_meta 消息端到端下发；
//   每块 64KB，AES-256-GCM；nonce 由文件密钥+序号派生（唯一）；
//   AAD 绑定 fileId、序号与上一块的认证标签 → 块间 MAC 链，
//   任何缺块/换序/篡改都会使后续块解密失败。

#include "common/base64.hpp"
#include "common/crypto/crypto.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace cc::files {

inline constexpr std::size_t kChunkSize = 64 * 1024;
inline constexpr std::size_t kFileIdLen = 16;

struct ChunkSeal {
    Bytes cipher;  // 密文（不含标签）
    Bytes tag;     // 本块认证标签 16B（作为下一块的链式 AAD）
};

// 派生第 seq 块的 12B nonce
Bytes chunkNonce(const Bytes& fileKey, std::uint32_t seq);

// 加密一块。prevTag 为空表示第一块。
ChunkSeal sealChunk(const Bytes& fileKey, BytesView fileId,
                    std::uint32_t seq, BytesView prevTag, BytesView plain);

// 解密一块；校验失败返回 nullopt。
std::optional<Bytes> openChunk(const Bytes& fileKey, BytesView fileId,
                               std::uint32_t seq, BytesView prevTag,
                               BytesView cipher, BytesView tag);

// 读文件全量（课程设计规模：演示文件不超过数十 MB）
bool readFile(const std::string& path, Bytes& out);
bool writeFile(const std::string& path, const Bytes& data);

}  // namespace cc::files
