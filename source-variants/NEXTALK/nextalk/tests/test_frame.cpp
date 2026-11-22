#include <doctest/doctest.h>

#include "common/protocol/frame.hpp"

TEST_CASE("帧编码产生大端长度前缀") {
    const std::string payload = R"({"type":"ping"})";  // 15 字节
    const std::string frame = cc::proto::encodeFrame(payload);
    REQUIRE(frame.size() == 4 + payload.size());
    const unsigned char* h =
        reinterpret_cast<const unsigned char*>(frame.data());
    CHECK(h[0] == 0);
    CHECK(h[1] == 0);
    CHECK(h[2] == 0);
    CHECK(h[3] == 15);
    CHECK(frame.substr(4) == payload);
}

TEST_CASE("帧解码：完整一帧") {
    const std::string payload = R"({"hello":"世界","n":42})";
    const std::string frame = cc::proto::encodeFrame(payload);
    cc::proto::DecodedFrame out;
    CHECK(cc::proto::decodeFrame(frame, out) == cc::proto::DecodeResult::Ok);
    CHECK(out.payload == payload);
    CHECK(out.consumed == frame.size());
}

TEST_CASE("帧解码：分片到达逐步等待") {
    const std::string payload(200, 'x');
    const std::string frame = cc::proto::encodeFrame(payload);
    for (std::size_t cut = 0; cut < frame.size(); ++cut) {
        cc::proto::DecodedFrame out;
        CHECK(cc::proto::decodeFrame(std::string_view(frame).substr(0, cut),
                                     out) ==
              cc::proto::DecodeResult::NeedMoreData);
    }
    cc::proto::DecodedFrame out;
    CHECK(cc::proto::decodeFrame(frame, out) == cc::proto::DecodeResult::Ok);
}

TEST_CASE("帧解码：粘包只解第一帧并报告消耗") {
    const std::string a = cc::proto::encodeFrame(R"({"a":1})");
    const std::string b = cc::proto::encodeFrame(R"({"b":"two"})");
    const std::string stream = a + b + std::string("par");  // 末尾半截帧头
    cc::proto::DecodedFrame out;
    REQUIRE(cc::proto::decodeFrame(stream, out) == cc::proto::DecodeResult::Ok);
    CHECK(out.payload == R"({"a":1})");
    CHECK(out.consumed == a.size());
    // 第二帧
    std::string_view rest(stream.data() + out.consumed,
                          stream.size() - out.consumed);
    REQUIRE(cc::proto::decodeFrame(rest, out) == cc::proto::DecodeResult::Ok);
    CHECK(out.payload == R"({"b":"two"})");
    CHECK(out.consumed == b.size());
    // 剩余半截
    std::string_view tail(rest.data() + out.consumed,
                          rest.size() - out.consumed);
    CHECK(cc::proto::decodeFrame(tail, out) ==
          cc::proto::DecodeResult::NeedMoreData);
}

TEST_CASE("帧解码：超限与非法帧头") {
    cc::proto::DecodedFrame out;
    // 声明超过上限的长度（0x00FFFFFF + 1 = 16MB+1 > 8MB）
    std::string big(4, '\0');
    big[2] = '\x01';  // 长度 = 0x00000100...? 构造明确超限值
    big[0] = 0x00; big[1] = 0x00; big[2] = 0x00; big[3] = 0x01;
    // 重新构造：长度 = 0x00900001（> 8MB 上限）
    big[1] = static_cast<char>(0x90);
    CHECK(cc::proto::decodeFrame(big, out) == cc::proto::DecodeResult::TooLarge);

    std::string zero(4, '\0');
    CHECK(cc::proto::decodeFrame(zero, out) == cc::proto::DecodeResult::Invalid);
}

TEST_CASE("帧编码：大载荷往返") {
    const std::string payload(1024 * 1024, 'y');  // 1 MiB，模拟文件分块
    const std::string frame = cc::proto::encodeFrame(payload);
    cc::proto::DecodedFrame out;
    REQUIRE(cc::proto::decodeFrame(frame, out) == cc::proto::DecodeResult::Ok);
    CHECK(out.payload.size() == 1024 * 1024);
}
