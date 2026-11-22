// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_FILE_CACHE_STORE_HPP
#define NEXTALK_CHAT_FILE_CACHE_STORE_HPP

#include "sqlite_core.hpp"
#include <stdint.h>

namespace nextalk {
namespace chat {

struct CachePosition {
    uint32_t next;
    bool complete;
    CachePosition(uint32_t position, bool finished) : next(position), complete(finished) {}
};

class FileCacheStore {
public:
    FileCacheStore(uint64_t capacity, int64_t lifetime);
    void open(const std::string& path);
    CachePosition begin(const std::string& owner, const std::string& id,
                        const std::string& recipient, uint64_t size, uint32_t chunks, int64_t now);
    uint32_t put(const std::string& owner, const std::string& id, uint32_t sequence,
                 const std::string& data, int64_t now);
    bool get(const std::string& actor, const std::string& owner, const std::string& id,
             uint32_t sequence, int64_t now, std::string& data);
    void cancel(const std::string& owner, const std::string& id);
private:
    void prune(int64_t now);
    void changeUsage(sqlite3_int64 difference);
    ccfw::sql::Database db_;
    uint64_t capacity_;
    int64_t lifetime_;
};

} // namespace chat
} // namespace nextalk

#endif
