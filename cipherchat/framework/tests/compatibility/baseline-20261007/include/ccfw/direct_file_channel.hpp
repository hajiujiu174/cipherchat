#pragma once

#include "ccfw/result.hpp"
#include "ccfw/types.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ccfw {

// DirectFileChannel carries already encrypted file envelopes over a temporary
// authenticated peer socket. The control plane still uses ITransport/Relay.
class DirectFileChannel {
public:
    DirectFileChannel();
    ~DirectFileChannel();
    DirectFileChannel(const DirectFileChannel&) = delete;
    DirectFileChannel& operator=(const DirectFileChannel&) = delete;

    Result<DirectFileEndpoint> listen(const std::string& fileId, const std::string& token);
    // Starts an asynchronous attempt; connected()/failed() report its outcome.
    Result<> connect(const DirectFileEndpoint& endpoint);
    Result<> send(const WireEnvelope& envelope);
    Result<std::optional<WireEnvelope>> poll();
    bool connected() const noexcept;
    bool failed() const noexcept;
    const std::string& fileId() const noexcept;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ccfw
