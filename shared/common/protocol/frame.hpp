#pragma once
// 应用层帧编解码：4 字节大端长度前缀 + JSON 文本载荷。
// 客户端与服务器之间的所有消息都以该帧格式在 TLS 通道上传输。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cc::proto {

// 帧头固定 4 字节；单帧上限保护内存（文件分块最大 1 MiB，留余量）。
inline constexpr std::uint32_t kFrameHeaderSize = 4;
inline constexpr std::uint32_t kMaxFrameSize = 8u * 1024 * 1024;

// 编码一帧。payload 为 UTF-8 JSON 文本。
std::string encodeFrame(std::string_view payload);

enum class DecodeResult {
    Ok,            // 成功解出一帧，out 已填充
    NeedMoreData,  // 数据不完整，继续积累
    TooLarge,      // 帧长度超过 kMaxFrameSize，连接应断开
    Invalid,       // 帧头非法（如声明长度为 0），连接应断开
};

struct DecodedFrame {
    std::string payload;   // JSON 文本
    std::size_t consumed;  // 本次从缓冲区消耗的字节数
};

// 从 buffer 开头尝试解出一帧。
DecodeResult decodeFrame(std::string_view buffer, DecodedFrame& out);

}  // namespace cc::proto
