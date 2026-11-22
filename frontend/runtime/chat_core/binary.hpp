// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_BINARY_HPP
#define NEXTALK_CHAT_BINARY_HPP

#include <cstddef>
#include <stdint.h>
#include <string>

namespace nextalk {
namespace chat {

const uint32_t kFrameHeaderSize = 4;
const uint32_t kMaxFrameSize = 8u * 1024 * 1024;

enum FrameResult { FrameOk, FrameNeedMoreData, FrameTooLarge, FrameInvalid };

std::string base64Encode(const uint8_t* data, std::size_t size);
bool base64Decode(const char* data, std::size_t size, std::string& out);
std::string encodeFrame(const char* data, std::size_t size);
FrameResult decodeFrame(const char* data, std::size_t size, std::string& payload, std::size_t& consumed);

} // namespace chat
} // namespace nextalk

#endif
