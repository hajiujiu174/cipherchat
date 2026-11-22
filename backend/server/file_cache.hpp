#pragma once
#include "file_cache_store.hpp"
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>

namespace cc::server {

// 在协议层转换 JSON 与 C++98 文件缓存的返回值。
class FileCache {
public:
    using Json = nlohmann::json;
    explicit FileCache(std::uint64_t capacity = 10ULL * 1024 * 1024 * 1024,
                       std::int64_t lifetime = 30 * 86400)
        : store_(capacity, lifetime) {}
    void open(const std::string& path) { store_.open(path); }

    static std::int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    Json begin(const std::string& owner, const std::string& id, const std::string& recipient,
               std::uint64_t size, std::uint32_t chunks, std::int64_t time = now()) {
        const auto position = store_.begin(owner, id, recipient, size, chunks, time);
        return {{"next", position.next}, {"complete", position.complete}};
    }
    std::uint32_t put(const std::string& actor, const std::string& id, std::uint32_t seq,
                      const std::string& data, std::int64_t time = now()) {
        return store_.put(actor, id, seq, data, time);
    }
    std::optional<std::string> get(const std::string& actor, const std::string& owner,
                                   const std::string& id, std::uint32_t seq, std::int64_t time = now()) {
        std::string block;
        if (!store_.get(actor, owner, id, seq, time, block)) return std::nullopt;
        return block;
    }
    void cancel(const std::string& owner, const std::string& id) { store_.cancel(owner, id); }
private:
    nextalk::chat::FileCacheStore store_;
};

} // namespace cc::server
