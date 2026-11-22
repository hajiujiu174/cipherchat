// 人工实现，由 ChatGPT 规范化。

#include "binary.hpp"

namespace nextalk {
namespace chat {
namespace {

const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 解码Char，交给调用方区分有效结果与错误。
int8_t decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

// 把任意字节转换为便于放入 JSON 或文本字段的 Base64 字符串。
std::string base64Encode(const uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= size) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8) |
                                static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back(kAlphabet[n & 0x3F]);
        i += 3;
    }
    const std::size_t remain = size - i;
    if (remain == 1) {
        const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remain == 2) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// 验证 Base64 字符、填充和尾部位，再还原原始字节。
bool base64Decode(const char* text, std::size_t size, std::string& out) {
    if (size % 4 != 0) return false;
    out.clear();
    out.reserve(size / 4 * 3);
    uint32_t buffer = 0;
    int bits = 0;
    std::size_t padding = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const char c = text[i];
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding > 0) return false;  // '=' 之后不允许再出现数据字符
        const int8_t v = decodeChar(c);
        if (v < 0) return false;
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    // 填充数量只能为 0/1/2，且与末组剩余位一致。
    if (padding > 2) return false;
    if (padding > 0 && bits != static_cast<int>(padding * 2)) return false;
    return true;
}

// 加上四字节大端长度头，把载荷组成可通过字节流发送的完整帧。
std::string encodeFrame(const char* payload, std::size_t size) {
    const uint32_t length = static_cast<uint32_t>(size);
    std::string frame;
    frame.reserve(kFrameHeaderSize + size);
    frame.push_back(static_cast<char>((length >> 24) & 0xFF));
    frame.push_back(static_cast<char>((length >> 16) & 0xFF));
    frame.push_back(static_cast<char>((length >> 8) & 0xFF));
    frame.push_back(static_cast<char>(length & 0xFF));
    if (size) frame.append(payload, size);
    return frame;
}

// 校验缓冲区头部并在数据足够时取出一帧，返回已消耗的字节数。
FrameResult decodeFrame(const char* buffer, std::size_t size, std::string& payload, std::size_t& consumed) {
    if (size < kFrameHeaderSize) return FrameNeedMoreData;
    const uint32_t length =
        (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
    if (length == 0) return FrameInvalid;
    if (length > kMaxFrameSize) return FrameTooLarge;
    if (size < kFrameHeaderSize + length) return FrameNeedMoreData;
    payload.assign(buffer + kFrameHeaderSize, length);
    consumed = kFrameHeaderSize + length;
    return FrameOk;
}

} // namespace chat
} // namespace nextalk
