#include "ccfw/transport.hpp"
#include "client/core/net_client.hpp"
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
namespace ccfw {
namespace {
Error transportError(ErrorCode code) { return {code, "M3-01", "安全连接不可用或网络数据非法"}; }
ErrorCode mapReason(cc::client::NetClient::CloseReason reason) {
    using Reason = cc::client::NetClient::CloseReason;
    if (reason == Reason::AuthenticationFailed) return ErrorCode::AuthenticationFailed;
    if (reason == Reason::ProtocolError) return ErrorCode::ProtocolError;
    if (reason == Reason::Timeout) return ErrorCode::Timeout;
    return ErrorCode::NotConnected;
}
}
struct TlsTransport::Impl {
    std::mutex mutex;
    std::deque<std::pair<cc::proto::Message, std::size_t>> incoming;
    std::size_t incomingBytes = 0;
    std::optional<Error> failure;
    cc::client::NetClient network;
};
TlsTransport::TlsTransport() = default;
TlsTransport::~TlsTransport() { close(); }
Result<> TlsTransport::connect(const ConnectionOptions& options) {
    close();
    if (options.host.empty() || !options.port || options.expectedServerName.empty() || options.caFile.empty() ||
        !options.timeoutMs || options.timeoutMs > 60000) return Result<>::failure(transportError(ErrorCode::InvalidArgument));
    try {
        if (std::filesystem::file_size(options.caFile) > 1024 * 1024) return Result<>::failure(transportError(ErrorCode::InvalidArgument));
        std::ifstream input(options.caFile, std::ios::binary);
        if (!input) return Result<>::failure(transportError(ErrorCode::InvalidArgument));
        const std::string ca{std::istreambuf_iterator<char>(input), {}};
        auto candidate = std::make_unique<Impl>();
        Impl* state = candidate.get();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeoutMs);
        state->network.startSecure(options.host, options.port, ca, options.expectedServerName,
            [state](const cc::proto::Message& message) {
                const auto bytes = cc::proto::serialize(message).size();
                std::lock_guard lock(state->mutex);
                if (state->incoming.size() >= 256 || state->incomingBytes + bytes > 16 * 1024 * 1024) {
                    state->failure = transportError(ErrorCode::ProtocolError);
                    state->network.stop(); // 在网络回调仅发停止请求，不 join 自己。
                    return;
                }
                state->incoming.emplace_back(message, bytes);
                state->incomingBytes += bytes;
            }, [state] {
                std::lock_guard lock(state->mutex);
                if (!state->failure) state->failure = transportError(mapReason(state->network.closeReason()));
            }, options.timeoutMs);
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0 || !state->network.waitHandshake(static_cast<std::uint32_t>(remaining))) {
            auto code = mapReason(state->network.closeReason());
            if (state->network.connected() || remaining <= 0) code = ErrorCode::Timeout;
            state->network.stop();
            return Result<>::failure(transportError(code));
        }
        impl_ = std::move(candidate);
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(transportError(ErrorCode::NotConnected)); }
}
Result<> TlsTransport::send(const cc::proto::Message& message) {
    if (!impl_) return Result<>::failure(transportError(ErrorCode::NotConnected));
    try {
        if (!impl_->network.send(message)) return Result<>::failure(transportError(
            impl_->network.connected() ? ErrorCode::Forbidden : ErrorCode::NotConnected));
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(transportError(ErrorCode::ProtocolError)); }
}
Result<std::optional<cc::proto::Message>> TlsTransport::poll() {
    if (!impl_) return Result<std::optional<cc::proto::Message>>::failure(transportError(ErrorCode::NotConnected));
    std::lock_guard lock(impl_->mutex);
    if (!impl_->incoming.empty()) {
        auto message = std::move(impl_->incoming.front()); impl_->incoming.pop_front();
        impl_->incomingBytes -= message.second;
        return Result<std::optional<cc::proto::Message>>::success(std::move(message.first));
    }
    if (impl_->failure) return Result<std::optional<cc::proto::Message>>::failure(*impl_->failure);
    return Result<std::optional<cc::proto::Message>>::success(std::nullopt);
}
void TlsTransport::close() noexcept {
    if (impl_) impl_->network.stop();
    impl_.reset();
}
}
