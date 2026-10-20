#pragma once
// Base64 编解码（RFC 4648，标准字母表，含 '=' 填充）。
// 协议中所有二进制密钥材料、密文均以 Base64 字符串形式内嵌 JSON 传输。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cc {

// 二进制数据的统一别名：密钥、密文、签名等均以字节串表示。
using Bytes = std::string;
// 只读字节串视图：接口入参统一用它，避免拷贝。
using BytesView = std::string_view;

// 编码，输出标准 Base64。输入可为空，输出为空。
std::string base64Encode(const std::uint8_t* data, std::size_t size);
std::string base64Encode(std::string_view bytes);

// 解码。输入含非法字符或长度不合法时返回 false 且不修改 out。
bool base64Decode(std::string_view text, Bytes& out);

}  // namespace cc
