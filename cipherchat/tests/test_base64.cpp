#include <doctest/doctest.h>

#include "common/base64.hpp"

TEST_CASE("base64 空输入") {
    CHECK(cc::base64Encode("") == "");
    cc::Bytes out;
    CHECK(cc::base64Decode("", out));
    CHECK(out.empty());
}

TEST_CASE("base64 已知向量") {
    // RFC 4648 示例
    CHECK(cc::base64Encode("f") == "Zg==");
    CHECK(cc::base64Encode("fo") == "Zm8=");
    CHECK(cc::base64Encode("foo") == "Zm9v");
    CHECK(cc::base64Encode("foob") == "Zm9vYg==");
    CHECK(cc::base64Encode("fooba") == "Zm9vYmE=");
    CHECK(cc::base64Encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("base64 二进制往返") {
    for (std::size_t size : {1u, 2u, 3u, 15u, 16u, 17u, 31u, 32u, 33u, 255u}) {
        std::string input(size, '\0');
        for (std::size_t i = 0; i < size; ++i) {
            input[i] = static_cast<char>((i * 37 + 11) & 0xFF);
        }
        const std::string enc = cc::base64Encode(input);
        cc::Bytes dec;
        REQUIRE(cc::base64Decode(enc, dec));
        CHECK(dec == input);
    }
}

TEST_CASE("base64 非法输入被拒绝") {
    cc::Bytes out;
    CHECK_FALSE(cc::base64Decode("A", out));     // 长度非 4 倍数
    CHECK_FALSE(cc::base64Decode("====", out));  // 过多填充
    CHECK_FALSE(cc::base64Decode("A===", out));  // 填充与位数不符
    CHECK_FALSE(cc::base64Decode("AB=B", out));  // 填充后出现数据
    CHECK_FALSE(cc::base64Decode("AB!C", out));  // 非法字符
}
