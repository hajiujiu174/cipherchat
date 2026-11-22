// 人工实现，由 ChatGPT 规范化。

#include "snapshot_store.hpp"
#include <limits>

namespace nextalk {
namespace chat {

void initializeSnapshot(sqlite3* database, const std::string& server,
                        const std::string& user, const std::string& initial) {
    ccfw::sql::Transaction transaction(database);
    ccfw::sql::exec(database,
        "CREATE TABLE client_state ("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1), server_id TEXT NOT NULL,"
        "username TEXT NOT NULL, revision INTEGER NOT NULL CHECK(revision>=0),"
        "format_version INTEGER NOT NULL CHECK(format_version=1), protected_snapshot BLOB NOT NULL);"
        "PRAGMA user_version=1;");
    ccfw::sql::Statement insert(database, "INSERT INTO client_state VALUES(1,?1,?2,0,1,?3)");
    insert.text(1, server);
    insert.text(2, user);
    insert.blob(3, initial);
    insert.step();
    transaction.commit();
}

bool ensureSnapshotTables(sqlite3* database) {
    ccfw::sql::Statement version(database, "PRAGMA user_version");
    if (!version.step() || version.number(0) != 1) return false;
    ccfw::sql::exec(database,
        "CREATE TABLE IF NOT EXISTS client_features (singleton INTEGER PRIMARY KEY CHECK(singleton=1), "
        "format_version INTEGER NOT NULL CHECK(format_version=1), protected_features BLOB NOT NULL)");
    return true;
}

bool readSnapshot(sqlite3* database, const std::string& server,
                  const std::string& user, SnapshotRow& row) {
    ccfw::sql::Statement main(database,
        "SELECT server_id,username,revision,format_version,protected_snapshot FROM client_state WHERE singleton=1");
    if (!main.step() || main.bytes(0) != server || main.bytes(1) != user ||
        main.number(2) < 0 || main.number(3) != 1) {
        return false;
    }
    row.revision = static_cast<uint64_t>(main.number(2));
    row.mainBytes = main.bytes(4);
    row.featureBytes.clear();
    ccfw::sql::Statement feature(database,
        "SELECT format_version,protected_features FROM client_features WHERE singleton=1");
    if (feature.step()) {
        if (feature.number(0) != 1) return false;
        row.featureBytes = feature.bytes(1);
        if (row.featureBytes.empty()) return false;
    }
    return true;
}

// 只读打开旧快照，提取文件任务所需的原始记录。
bool readArchivedSnapshot(const std::string& path, std::string& snapshot) {
    ccfw::sql::Database backup;
    if (sqlite3_open_v2(path.c_str(), &backup.value, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) {
        return false;
    }
    ccfw::sql::Statement row(backup.value, "SELECT protected_snapshot FROM client_state WHERE singleton=1");
    if (!row.step()) return false;
    snapshot = row.bytes(0);
    return true;
}

bool legacyFilePacket(const std::set<std::string>& files, const std::string& kind,
                      const std::string& id) {
    if (kind != "file_meta" && kind != "file_ack" && kind != "file_chunk") return false;
    for (std::set<std::string>::const_iterator file = files.begin(); file != files.end(); ++file) {
        const std::string ack = "fack:" + *file + ":";
        const std::string chunk = "fchunk:" + *file + ":";
        if (id == "fmeta:" + *file || id.compare(0, ack.size(), ack) == 0 ||
            id.compare(0, chunk.size(), chunk) == 0) {
            return true;
        }
    }
    return false;
}

bool validSnapshotRevision(uint64_t expected, uint64_t next) {
    return expected < static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max()) &&
        next == expected + 1;
}

bool updateSnapshot(sqlite3* database, const std::string& server,
                    const std::string& user, uint64_t expected, const SnapshotRow& row) {
    if (!validSnapshotRevision(expected, row.revision)) return false;
    ccfw::sql::Statement update(database,
        "UPDATE client_state SET revision=?1,protected_snapshot=?2 "
        "WHERE singleton=1 AND revision=?3 AND server_id=?4 AND username=?5");
    update.number(1, static_cast<sqlite3_int64>(row.revision));
    update.blob(2, row.mainBytes);
    update.number(3, static_cast<sqlite3_int64>(expected));
    update.text(4, server);
    update.text(5, user);
    update.step();
    if (sqlite3_changes(database) != 1) return false;

    ccfw::sql::Statement feature(database,
        "INSERT INTO client_features VALUES(1,1,?1) "
        "ON CONFLICT(singleton) DO UPDATE SET protected_features=excluded.protected_features");
    feature.blob(1, row.featureBytes);
    feature.step();
    return true;
}

} // namespace chat
} // namespace nextalk
