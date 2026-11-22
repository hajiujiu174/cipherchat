#pragma once
#include <sqlite3.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ccfw::sql {
inline void require(int result) {
    if (result != SQLITE_OK) throw std::runtime_error("SQLite operation failed");
}
inline void exec(sqlite3* database, const char* statement) {
    require(sqlite3_exec(database, statement, nullptr, nullptr, nullptr));
}
struct Database {
    sqlite3* value = nullptr;
    Database() = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database() { if (value) sqlite3_close_v2(value); }
    void open(const std::string& path) {
        require(sqlite3_open_v2(path.empty() ? ":memory:" : path.c_str(), &value,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr));
        require(sqlite3_busy_timeout(value, 1000));
        exec(value, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    }
};
struct Statement {
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* database, const char* text) {
        require(sqlite3_prepare_v2(database, text, -1, &value, nullptr));
    }
    ~Statement() { if (value) sqlite3_finalize(value); }
    Statement(const Statement&) = delete;
    void text(int index, std::string_view data) {
        require(sqlite3_bind_text64(value, index, data.data(), data.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
    }
    void blob(int index, std::string_view data) {
        require(sqlite3_bind_blob64(value, index, data.data(), data.size(), SQLITE_TRANSIENT));
    }
    void number(int index, std::int64_t data) { require(sqlite3_bind_int64(value, index, data)); }
    bool step() {
        const auto result = sqlite3_step(value);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        throw std::runtime_error("SQLite statement failed");
    }
    std::string bytes(int index) const {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(value, index));
        return data ? std::string(data, sqlite3_column_bytes(value, index)) : std::string{};
    }
    std::int64_t number(int index) const { return sqlite3_column_int64(value, index); }
};
struct Transaction {
    sqlite3* database;
    bool committed = false;
    explicit Transaction(sqlite3* db) : database(db) { exec(database, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed) sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(database, "COMMIT"); committed = true; }
};
} // namespace ccfw::sql
