#pragma once
#include "ccfw/result.hpp"
#include "ccfw/types.hpp"
#include <memory>

namespace ccfw {
// 同一业务线程调用 connect/send/poll/close；poll 非阻塞。
// 实现可自建网络线程，但只能向内部队列投递，不能调用 GUI 或业务回调。
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual Result<> connect(const ConnectionOptions&) = 0;
    virtual Result<> send(const cc::proto::Message&) = 0;
    // nullopt = 当前无消息；断线/解码失败返回错误，不能伪装成空队列
    virtual Result<std::optional<cc::proto::Message>> poll() = 0;
    virtual void close() noexcept = 0; // 幂等，停线程后才释放 socket/TLS
};
class TlsTransport final : public ITransport {
public:
    TlsTransport();
    ~TlsTransport() override;
    Result<> connect(const ConnectionOptions&) override;
    Result<> send(const cc::proto::Message&) override;
    Result<std::optional<cc::proto::Message>> poll() override;
    void close() noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ccfw

