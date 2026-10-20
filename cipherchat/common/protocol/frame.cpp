#include "common/protocol/frame.hpp"

namespace cc::proto {

std::string encodeFrame(std::string_view payload) {
    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    std::string frame;
    frame.reserve(kFrameHeaderSize + payload.size());
    frame.push_back(static_cast<char>((length >> 24) & 0xFF));
    frame.push_back(static_cast<char>((length >> 16) & 0xFF));
    frame.push_back(static_cast<char>((length >> 8) & 0xFF));
    frame.push_back(static_cast<char>(length & 0xFF));
    frame.append(payload);
    return frame;
}

DecodeResult decodeFrame(std::string_view buffer, DecodedFrame& out) {
    if (buffer.size() < kFrameHeaderSize) return DecodeResult::NeedMoreData;
    const std::uint32_t length =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[3]));
    if (length == 0) return DecodeResult::Invalid;
    if (length > kMaxFrameSize) return DecodeResult::TooLarge;
    if (buffer.size() < kFrameHeaderSize + length) return DecodeResult::NeedMoreData;
    out.payload.assign(buffer.data() + kFrameHeaderSize, length);
    out.consumed = kFrameHeaderSize + length;
    return DecodeResult::Ok;
}

}  // namespace cc::proto
