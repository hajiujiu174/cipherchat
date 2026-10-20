#include "base64.hpp"

namespace cc {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int8_t decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= size) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back(kAlphabet[n & 0x3F]);
        i += 3;
    }
    const std::size_t remain = size - i;
    if (remain == 1) {
        const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remain == 2) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string base64Encode(std::string_view bytes) {
    return base64Encode(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                        bytes.size());
}

bool base64Decode(std::string_view text, Bytes& out) {
    if (text.size() % 4 != 0) return false;
    out.clear();
    out.reserve(text.size() / 4 * 3);
    std::uint32_t buffer = 0;
    int bits = 0;
    std::size_t padding = 0;
    for (char c : text) {
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding > 0) return false;  // '=' 之后不允许再出现数据字符
        const int8_t v = decodeChar(c);
        if (v < 0) return false;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    // 填充数量只能为 0/1/2，且与末组剩余位一致。
    if (padding > 2) return false;
    if (padding > 0 && bits != padding * 2) return false;
    return true;
}

}  // namespace cc
