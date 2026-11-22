// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_SNAPSHOT_STORE_HPP
#define NEXTALK_CHAT_SNAPSHOT_STORE_HPP

#include "sqlite_core.hpp"
#include <stdint.h>
#include <set>

namespace nextalk {
namespace chat {

struct SnapshotRow {
    uint64_t revision;
    std::string mainBytes;
    std::string featureBytes;
    SnapshotRow() : revision(0) {}
};

void initializeSnapshot(sqlite3* database, const std::string& server,
                        const std::string& user, const std::string& initial);
bool ensureSnapshotTables(sqlite3* database);
bool readSnapshot(sqlite3* database, const std::string& server,
                  const std::string& user, SnapshotRow& row);
bool validSnapshotRevision(uint64_t expected, uint64_t next);
bool readArchivedSnapshot(const std::string& path, std::string& snapshot);
bool legacyFilePacket(const std::set<std::string>& files, const std::string& kind,
                      const std::string& id);

// 调用方先在同一事务中读取并验证旧快照，提交仍由调用方负责。
bool updateSnapshot(sqlite3* database, const std::string& server,
                    const std::string& user, uint64_t expected, const SnapshotRow& row);

} // namespace chat
} // namespace nextalk

#endif
