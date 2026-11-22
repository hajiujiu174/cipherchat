#pragma once
#include "ccfw/result.hpp"
#include "ccfw/types.hpp"
#include <memory>

namespace ccfw {
// 同一业务线程调用 connect/send/poll/close；poll 非阻塞。
// 实现可自建网络线程，但只能向内部队列投递，不能调用 GUI 或业务回调。
class ITransport {
public:
    // 销毁传输接口时释放它持有的资源。
    virtual ~ITransport() = default;
    // 建立连接传输接口。
    virtual Result<> connect(const ConnectionOptions&) = 0;
    // 发送传输接口。
    virtual Result<> send(const cc::proto::Message&) = 0;
    // nullopt = 当前无消息；断线/解码失败返回错误，不能伪装成空队列
    virtual Result<std::optional<cc::proto::Message>> poll() = 0;
    // 关闭传输接口。
    virtual void close() noexcept = 0; // 幂等，停线程后才释放 socket/TLS
};
class TlsTransport final : public ITransport {
public:
    // 构造TLS 传输适配器，初始化后续操作需要的状态或依赖。
    TlsTransport();
    // 销毁TLS 传输适配器时释放它持有的资源。
    ~TlsTransport() override;
    // 建立连接TLS 传输适配器。
    Result<> connect(const ConnectionOptions&) override;
    // 发送TLS 传输适配器。
    Result<> send(const cc::proto::Message&) override;
    // 取出下一条已解帧消息或返回当前网络错误，供业务线程分发。
    Result<std::optional<cc::proto::Message>> poll() override;
    // 关闭TLS 传输适配器。
    void close() noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ccfw

