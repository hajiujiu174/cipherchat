#pragma once
#include <cstddef>
#include <cstdint>

namespace cc::auth {

// 注册和重置密码所用的默认参数。登录使用各账号注册时存下的参数。
inline constexpr std::int64_t kPbkdf2Iterations = 200000;
inline constexpr std::size_t kPbkdf2SaltLen = 16;
inline constexpr std::size_t kPbkdf2KeyLen = 32;

}  // namespace cc::auth
