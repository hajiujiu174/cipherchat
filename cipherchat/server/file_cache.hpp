#pragma once
#include "framework/src/sqlite_helpers.hpp"
#include "common/base64.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <stdexcept>
#include <optional>

namespace cc::server {
// The router owns this connection and calls it on its existing serialized path.
// A separate database keeps cached ciphertext out of account/message backups.
class FileCache {
    ccfw::sql::Database db_;
    std::uint64_t capacity_;
    std::int64_t lifetime_;
    using Statement = ccfw::sql::Statement;
    using Transaction = ccfw::sql::Transaction;
    static void require(bool value) { if (!value) throw std::runtime_error("file cache request rejected"); }
public:
    using Json = nlohmann::json;
    explicit FileCache(std::uint64_t capacity = 10ULL*1024*1024*1024, std::int64_t lifetime = 30*86400)
        : capacity_(capacity),lifetime_(lifetime) {}
    void open(const std::string& path) {
        db_.open(path);
        ccfw::sql::exec(db_.value,R"(PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS cached_files(owner TEXT NOT NULL,id TEXT NOT NULL,recipient TEXT NOT NULL,
 size INTEGER NOT NULL,chunks INTEGER NOT NULL,next INTEGER NOT NULL DEFAULT 0,stored INTEGER NOT NULL DEFAULT 0,
 expires INTEGER NOT NULL,PRIMARY KEY(owner,id));
CREATE TABLE IF NOT EXISTS cached_blocks(owner TEXT NOT NULL,id TEXT NOT NULL,seq INTEGER NOT NULL,data BLOB NOT NULL,
 PRIMARY KEY(owner,id,seq),FOREIGN KEY(owner,id) REFERENCES cached_files(owner,id) ON DELETE CASCADE);
CREATE INDEX IF NOT EXISTS cache_expiry ON cached_files(expires);
CREATE TABLE IF NOT EXISTS cache_usage(id INTEGER PRIMARY KEY CHECK(id=1),bytes INTEGER NOT NULL);
INSERT OR IGNORE INTO cache_usage VALUES(1,0);)");
    }
    static std::int64_t now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
    Json begin(const std::string& owner,const std::string& id,const std::string& recipient,
               std::uint64_t size,std::uint32_t chunks,std::int64_t time = now()) {
        require(!owner.empty() && !recipient.empty() && owner != recipient && size <= 1024ULL*1024*1024 && chunks == (size+65535)/65536);
        Transaction tx(db_.value);
        Statement freed(db_.value,"SELECT COALESCE(SUM(stored),0) FROM cached_files WHERE expires<=?"); freed.number(1,time); require(freed.step()); const auto removed = freed.number(0);
        Statement prune(db_.value,"DELETE FROM cached_files WHERE expires<=?"); prune.number(1,time); prune.step();
        Statement usage(db_.value,"UPDATE cache_usage SET bytes=bytes-? WHERE id=1"); usage.number(1,removed); usage.step();
        Statement prior(db_.value,"SELECT recipient,size,chunks,next FROM cached_files WHERE owner=? AND id=?"); prior.text(1,owner); prior.text(2,id);
        std::uint32_t next = 0;
        if (prior.step()) {
            require(prior.bytes(0)==recipient && prior.number(1)==static_cast<std::int64_t>(size) && prior.number(2)==chunks);
            next = static_cast<std::uint32_t>(prior.number(3));
        } else {
            Statement count(db_.value,"SELECT COUNT(*) FROM cached_files"); require(count.step() && count.number(0)<10000);
            Statement insert(db_.value,"INSERT INTO cached_files(owner,id,recipient,size,chunks,expires) VALUES(?,?,?,?,?,?)");
            insert.text(1,owner);insert.text(2,id);insert.text(3,recipient);insert.number(4,size);insert.number(5,chunks);insert.number(6,time+lifetime_);insert.step();
        }
        tx.commit(); return {{"next",next},{"complete",next==chunks}};
    }
    std::uint32_t put(const std::string& actor,const std::string& id,std::uint32_t seq,const std::string& data,std::int64_t time = now()) {
        require(!data.empty() && data.size()<=128*1024 && data.starts_with("CCP4")); Transaction tx(db_.value);
        Statement file(db_.value,"SELECT next,chunks,expires FROM cached_files WHERE owner=? AND id=?"); file.text(1,actor);file.text(2,id);
        require(file.step() && file.number(2)>time); const auto next=static_cast<std::uint32_t>(file.number(0));require(seq<=next && seq<file.number(1));
        Statement prior(db_.value,"SELECT data FROM cached_blocks WHERE owner=? AND id=? AND seq=?");prior.text(1,actor);prior.text(2,id);prior.number(3,seq);
        if(prior.step()) { require(prior.bytes(0)==data);tx.commit();return next; }
        Statement usage(db_.value,"SELECT bytes FROM cache_usage WHERE id=1");require(usage.step() && std::uint64_t(usage.number(0))+data.size()<=capacity_);
        Statement insert(db_.value,"INSERT INTO cached_blocks VALUES(?,?,?,?)");insert.text(1,actor);insert.text(2,id);insert.number(3,seq);insert.blob(4,data);insert.step();
        Statement advance(db_.value,"UPDATE cached_files SET next=next+1,stored=stored+? WHERE owner=? AND id=?");advance.number(1,data.size());advance.text(2,actor);advance.text(3,id);advance.step();
        Statement used(db_.value,"UPDATE cache_usage SET bytes=bytes+? WHERE id=1");used.number(1,data.size());used.step();tx.commit();return next+1;
    }
    std::optional<std::string> get(const std::string& actor,const std::string& owner,const std::string& id,std::uint32_t seq,std::int64_t time = now()) {
        Statement file(db_.value,"SELECT recipient,chunks,expires FROM cached_files WHERE owner=? AND id=?");file.text(1,owner);file.text(2,id);
        require(file.step() && (actor==owner || actor==file.bytes(0)) && file.number(2)>time && seq<file.number(1));
        Statement block(db_.value,"SELECT data FROM cached_blocks WHERE owner=? AND id=? AND seq=?");block.text(1,owner);block.text(2,id);block.number(3,seq);
        if (!block.step()) return std::nullopt;
        return block.bytes(0);
    }
    void cancel(const std::string& owner,const std::string& id) {
        Transaction tx(db_.value);Statement size(db_.value,"SELECT stored FROM cached_files WHERE owner=? AND id=?");size.text(1,owner);size.text(2,id);
        if(size.step()) {
            const auto bytes=size.number(0);Statement drop(db_.value,"DELETE FROM cached_files WHERE owner=? AND id=?");drop.text(1,owner);drop.text(2,id);drop.step();
            Statement usage(db_.value,"UPDATE cache_usage SET bytes=bytes-? WHERE id=1");usage.number(1,bytes);usage.step();
        }
        tx.commit();
    }
};
}
