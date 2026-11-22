// 人工实现，由 ChatGPT 规范化。

#include "file_cache_store.hpp"

#include <stdexcept>

namespace nextalk {
namespace chat {
namespace {
using ccfw::sql::Statement;
using ccfw::sql::Transaction;

const uint64_t kMaxFileSize = uint64_t(1024) * 1024 * 1024;
const uint32_t kChunkSize = 65536;
const char kSchema[] =
    "PRAGMA foreign_keys=ON;\n"
    "CREATE TABLE IF NOT EXISTS cached_files(owner TEXT NOT NULL,id TEXT NOT NULL,recipient TEXT NOT NULL,\n"
    " size INTEGER NOT NULL,chunks INTEGER NOT NULL,next INTEGER NOT NULL DEFAULT 0,stored INTEGER NOT NULL DEFAULT 0,\n"
    " expires INTEGER NOT NULL,PRIMARY KEY(owner,id));\n"
    "CREATE TABLE IF NOT EXISTS cached_blocks(owner TEXT NOT NULL,id TEXT NOT NULL,seq INTEGER NOT NULL,data BLOB NOT NULL,\n"
    " PRIMARY KEY(owner,id,seq),FOREIGN KEY(owner,id) REFERENCES cached_files(owner,id) ON DELETE CASCADE);\n"
    "CREATE INDEX IF NOT EXISTS cache_expiry ON cached_files(expires);\n"
    "CREATE TABLE IF NOT EXISTS cache_usage(id INTEGER PRIMARY KEY CHECK(id=1),bytes INTEGER NOT NULL);\n"
    "INSERT OR IGNORE INTO cache_usage VALUES(1,0);\n";

void require(bool value) {
    if (!value) throw std::runtime_error("file cache request rejected");
}
}

FileCacheStore::FileCacheStore(uint64_t capacity, int64_t lifetime)
    : capacity_(capacity), lifetime_(lifetime) {}

void FileCacheStore::open(const std::string& path) {
    db_.open(path);
    ccfw::sql::exec(db_.value, kSchema);
}

// 文件块和总容量在同一事务内更新，取消或过期时同时释放容量。
void FileCacheStore::changeUsage(sqlite3_int64 difference) {
    Statement usage(db_.value, "UPDATE cache_usage SET bytes=bytes+? WHERE id=1");
    usage.number(1, difference);
    usage.step();
}

void FileCacheStore::prune(int64_t now) {
    Statement freed(db_.value, "SELECT COALESCE(SUM(stored),0) FROM cached_files WHERE expires<=?");
    freed.number(1, now);
    require(freed.step());
    const sqlite3_int64 removed = freed.number(0);
    Statement pruneFiles(db_.value, "DELETE FROM cached_files WHERE expires<=?");
    pruneFiles.number(1, now);
    pruneFiles.step();
    changeUsage(-removed);
}

// 按发件人和文件 ID 恢复已存在的缓存任务。
CachePosition FileCacheStore::begin(const std::string& owner, const std::string& id,
                                    const std::string& recipient, uint64_t size, uint32_t chunks,
                                    int64_t now) {
    require(!owner.empty() && !recipient.empty() && owner != recipient &&
            size <= kMaxFileSize && chunks == (size + kChunkSize - 1) / kChunkSize);
    Transaction transaction(db_.value);
    prune(now);
    Statement prior(db_.value, "SELECT recipient,size,chunks,next FROM cached_files WHERE owner=? AND id=?");
    prior.text(1, owner);
    prior.text(2, id);
    uint32_t next = 0;
    if (prior.step()) {
        require(prior.bytes(0) == recipient &&
                prior.number(1) == static_cast<sqlite3_int64>(size) && prior.number(2) == chunks);
        next = static_cast<uint32_t>(prior.number(3));
    } else {
        Statement count(db_.value, "SELECT COUNT(*) FROM cached_files");
        require(count.step() && count.number(0) < 10000);
        Statement insert(db_.value,
            "INSERT INTO cached_files(owner,id,recipient,size,chunks,expires) VALUES(?,?,?,?,?,?)");
        insert.text(1, owner);
        insert.text(2, id);
        insert.text(3, recipient);
        insert.number(4, size);
        insert.number(5, chunks);
        insert.number(6, now + lifetime_);
        insert.step();
    }
    transaction.commit();
    return CachePosition(next, next == chunks);
}

// 同序号重试仅接受原数据，推进偏移和写块一起提交。
uint32_t FileCacheStore::put(const std::string& owner, const std::string& id, uint32_t sequence,
                             const std::string& data, int64_t now) {
    require(!data.empty() && data.size() <= 128 * 1024 && data.compare(0, 4, "CCP4") == 0);
    Transaction transaction(db_.value);
    Statement file(db_.value, "SELECT next,chunks,expires FROM cached_files WHERE owner=? AND id=?");
    file.text(1, owner);
    file.text(2, id);
    require(file.step() && file.number(2) > now);
    const uint32_t next = static_cast<uint32_t>(file.number(0));
    require(sequence <= next && sequence < file.number(1));
    Statement prior(db_.value, "SELECT data FROM cached_blocks WHERE owner=? AND id=? AND seq=?");
    prior.text(1, owner);
    prior.text(2, id);
    prior.number(3, sequence);
    if (prior.step()) {
        require(prior.bytes(0) == data);
        transaction.commit();
        return next;
    }
    Statement usage(db_.value, "SELECT bytes FROM cache_usage WHERE id=1");
    require(usage.step() && static_cast<uint64_t>(usage.number(0)) + data.size() <= capacity_);
    Statement insert(db_.value, "INSERT INTO cached_blocks VALUES(?,?,?,?)");
    insert.text(1, owner);
    insert.text(2, id);
    insert.number(3, sequence);
    insert.blob(4, data);
    insert.step();
    Statement advance(db_.value,
        "UPDATE cached_files SET next=next+1,stored=stored+? WHERE owner=? AND id=?");
    advance.number(1, data.size());
    advance.text(2, owner);
    advance.text(3, id);
    advance.step();
    changeUsage(data.size());
    transaction.commit();
    return next + 1;
}

// 读取前校验账号权限、文件保留期和块号。
bool FileCacheStore::get(const std::string& actor, const std::string& owner, const std::string& id,
                         uint32_t sequence, int64_t now, std::string& data) {
    Statement file(db_.value, "SELECT recipient,chunks,expires FROM cached_files WHERE owner=? AND id=?");
    file.text(1, owner);
    file.text(2, id);
    require(file.step() && (actor == owner || actor == file.bytes(0)) &&
            file.number(2) > now && sequence < file.number(1));
    Statement block(db_.value, "SELECT data FROM cached_blocks WHERE owner=? AND id=? AND seq=?");
    block.text(1, owner);
    block.text(2, id);
    block.number(3, sequence);
    if (!block.step()) return false;
    data = block.bytes(0);
    return true;
}

void FileCacheStore::cancel(const std::string& owner, const std::string& id) {
    Transaction transaction(db_.value);
    Statement size(db_.value, "SELECT stored FROM cached_files WHERE owner=? AND id=?");
    size.text(1, owner);
    size.text(2, id);
    if (size.step()) {
        const sqlite3_int64 bytes = size.number(0);
        Statement drop(db_.value, "DELETE FROM cached_files WHERE owner=? AND id=?");
        drop.text(1, owner);
        drop.text(2, id);
        drop.step();
        changeUsage(-bytes);
    }
    transaction.commit();
}

} // namespace chat
} // namespace nextalk
