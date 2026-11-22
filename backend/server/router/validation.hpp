#pragma once

#include <algorithm>
#include <string_view>

namespace cc::server {

// 判断有效用户名条件是否成立。
inline bool isValidUsername(std::string_view name) {
    if (name.size() < 2 || name.size() > 32) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               static_cast<unsigned char>(c) >= 0x80;  // 允许中文用户名
    });
}

} // namespace cc::server
