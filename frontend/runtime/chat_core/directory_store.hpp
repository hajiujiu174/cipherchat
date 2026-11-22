// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_DIRECTORY_STORE_HPP
#define NEXTALK_CHAT_DIRECTORY_STORE_HPP

#include "sqlite_core.hpp"
#include <stdint.h>
#include <vector>

namespace nextalk {
namespace chat {

struct GroupRow {
    int64_t id;
    uint64_t revision;
    std::string name;
    std::vector<std::string> members;
    GroupRow() : id(0), revision(0) {}
};

struct OfflineRow {
    std::string sender;
    std::string payload;
    OfflineRow(const std::string& from, const std::string& bytes) : sender(from), payload(bytes) {}
};

// 操作服务端拥有的连接，沿用现有路由锁和数据库表。
class DirectoryStore {
public:
    explicit DirectoryStore(sqlite3* database) : db_(database) {}
    bool createGroup(const std::string& name, const std::vector<std::string>& members, int64_t& id);
    bool findGroup(int64_t id, GroupRow& group);
    std::vector<GroupRow> groupsForUser(const std::string& name);
    std::string requestFriend(const std::string& from, const std::string& to);
    bool respondFriend(const std::string& recipient, const std::string& sender, bool accepted);
    std::vector<std::string> friendRequests(const std::string& name, bool incoming);
    std::vector<std::string> friendsForUser(const std::string& name);
    std::vector<std::string> searchUsernames(const std::string& name, const std::string& query,
                                             const std::string& after);
    void enqueueOffline(int64_t userId, const std::string& sender, const std::string& bytes);
    std::vector<OfflineRow> drainOffline(int64_t userId);
private:
    bool accountId(const std::string& name, sqlite3_int64& id);
    sqlite3* db_;
};

} // namespace chat
} // namespace nextalk

#endif
