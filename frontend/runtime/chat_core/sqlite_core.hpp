// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_SQLITE_CORE_HPP
#define NEXTALK_CHAT_SQLITE_CORE_HPP

#include <sqlite3.h>
#include <cstddef>
#include <string>

namespace ccfw {
namespace sql {

void require(int result);
void exec(sqlite3* database, const char* statement);

struct Database {
    sqlite3* value;
    Database();
    ~Database();
    void open(const std::string& path);
    void swap(Database& other);
private:
    Database(const Database&);
    Database& operator=(const Database&);
};

struct Statement {
    sqlite3_stmt* value;
    Statement(sqlite3* database, const char* text);
    ~Statement();
    void text(int index, const char* data);
    void text(int index, const char* data, std::size_t size);
    void blob(int index, const char* data, std::size_t size);
    template<std::size_t Size> void text(int index, const char (&data)[Size]) {
        text(index, static_cast<const char*>(data));
    }
    template<class Bytes> void text(int index, const Bytes& data) {
        text(index, data.data(), data.size());
    }
    template<class Bytes> void blob(int index, const Bytes& data) {
        blob(index, data.data(), data.size());
    }
    void number(int index, sqlite3_int64 data);
    bool step();
    void reset();
    std::string bytes(int index) const;
    std::string columnText(int index) const;
    sqlite3_int64 number(int index) const;
private:
    Statement(const Statement&);
    Statement& operator=(const Statement&);
};

struct Transaction {
    sqlite3* database;
    bool committed;
    explicit Transaction(sqlite3* db);
    ~Transaction();
    void commit();
private:
    Transaction(const Transaction&);
    Transaction& operator=(const Transaction&);
};

} // namespace sql
} // namespace ccfw

#endif
