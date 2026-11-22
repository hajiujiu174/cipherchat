// 人工实现，由 ChatGPT 规范化。

#include "sqlite_core.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ccfw {
namespace sql {

void require(int result) {
    if (result != SQLITE_OK) throw std::runtime_error("SQLite operation failed");
}

void exec(sqlite3* database, const char* statement) {
    require(sqlite3_exec(database, statement, 0, 0, 0));
}

Database::Database() : value(0) {}

Database::~Database() {
    if (value) sqlite3_close_v2(value);
}

// 连接、超时及同步设置都成功后，才接管新句柄。
void Database::open(const std::string& path) {
    Database candidate;
    require(sqlite3_open_v2(path.empty() ? ":memory:" : path.c_str(), &candidate.value,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, 0));
    require(sqlite3_busy_timeout(candidate.value, 1000));
    exec(candidate.value, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    swap(candidate);
}

void Database::swap(Database& other) {
    std::swap(value, other.value);
}

Statement::Statement(sqlite3* database, const char* text) : value(0) {
    require(sqlite3_prepare_v2(database, text, -1, &value, 0));
}

Statement::~Statement() {
    if (value) sqlite3_finalize(value);
}

void Statement::text(int index, const char* data) {
    text(index, data, data ? std::strlen(data) : 0);
}

void Statement::text(int index, const char* data, std::size_t size) {
    require(sqlite3_bind_text64(value, index, data, size, SQLITE_TRANSIENT, SQLITE_UTF8));
}

void Statement::blob(int index, const char* data, std::size_t size) {
    require(sqlite3_bind_blob64(value, index, data, size, SQLITE_TRANSIENT));
}

void Statement::number(int index, sqlite3_int64 data) {
    require(sqlite3_bind_int64(value, index, data));
}

bool Statement::step() {
    const int result = sqlite3_step(value);
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw std::runtime_error("SQLite statement failed");
}

void Statement::reset() {
    require(sqlite3_reset(value));
    require(sqlite3_clear_bindings(value));
}

std::string Statement::bytes(int index) const {
    const char* data = static_cast<const char*>(sqlite3_column_blob(value, index));
    return data ? std::string(data, sqlite3_column_bytes(value, index)) : std::string();
}

std::string Statement::columnText(int index) const {
    const unsigned char* data = sqlite3_column_text(value, index);
    return data ? std::string(reinterpret_cast<const char*>(data),
                              sqlite3_column_bytes(value, index)) : std::string();
}

sqlite3_int64 Statement::number(int index) const {
    return sqlite3_column_int64(value, index);
}

Transaction::Transaction(sqlite3* db) : database(db), committed(false) {
    exec(database, "BEGIN IMMEDIATE");
}

Transaction::~Transaction() {
    if (!committed) sqlite3_exec(database, "ROLLBACK", 0, 0, 0);
}

void Transaction::commit() {
    exec(database, "COMMIT");
    committed = true;
}

} // namespace sql
} // namespace ccfw
