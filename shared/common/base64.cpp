#include "base64.hpp"
#include "binary.hpp"

namespace cc {
std::string base64Encode(const std::uint8_t* data, std::size_t size) {
    return nextalk::chat::base64Encode(data, size);
}
std::string base64Encode(std::string_view bytes) {
    return base64Encode(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}
bool base64Decode(std::string_view text, Bytes& out) {
    return nextalk::chat::base64Decode(text.data(), text.size(), out);
}
} // namespace cc
