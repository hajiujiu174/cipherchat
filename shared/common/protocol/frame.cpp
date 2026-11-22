#include "common/protocol/frame.hpp"
#include "binary.hpp"

namespace cc::proto {
std::string encodeFrame(std::string_view payload) {
    return nextalk::chat::encodeFrame(payload.data(), payload.size());
}
DecodeResult decodeFrame(std::string_view buffer, DecodedFrame& out) {
    switch (nextalk::chat::decodeFrame(buffer.data(), buffer.size(), out.payload, out.consumed)) {
    case nextalk::chat::FrameOk: return DecodeResult::Ok;
    case nextalk::chat::FrameNeedMoreData: return DecodeResult::NeedMoreData;
    case nextalk::chat::FrameTooLarge: return DecodeResult::TooLarge;
    case nextalk::chat::FrameInvalid: return DecodeResult::Invalid;
    }
    return DecodeResult::Invalid;
}
} // namespace cc::proto
