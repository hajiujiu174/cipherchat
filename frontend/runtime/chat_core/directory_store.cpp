// 人工实现，由 ChatGPT 规范化。

#include "directory_store.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace nextalk {
namespace chat {
namespace {
using ccfw::sql::Statement;
using ccfw::sql::Transaction;

bool contains(const std::vector<std::string>& names, const std::string& value) {
    return std::find(names.begin(), names.end(), value) != names.end();
}

std::vector<std::string> readNames(Statement& statement) {
    std::vector<std::string> result;
    while (statement.step()) result.push_back(statement.columnText(0));
    return result;
}
}

bool DirectoryStore::accountId(const std::string& name, sqlite3_int64& id) {
    Statement query(db_, "SELECT id FROM accounts WHERE username=?1");
    query.text(1, name);
    if (!query.step()) return false;
    id = query.number(0);
    return true;
}

bool DirectoryStore::createGroup(const std::string& name, const std::vector<std::string>& members,
                                  int64_t& id) {
    if (name.empty() || name.size() > 128 || members.size() < 2 || members.size() > 64 ||
        std::set<std::string>(members.begin(), members.end()).size() != members.size()) return false;
    for (std::size_t i = 0; i < members.size(); ++i) {
        sqlite3_int64 ignored = 0;
        if (!accountId(members[i], ignored) || groupsForUser(members[i]).size() >= 128)
            return false;
    }
    try {
        Transaction transaction(db_);
        Statement group(db_, "INSERT INTO groups (name) VALUES (?1)");
        group.text(1, name);
        group.step();
        const sqlite3_int64 created = sqlite3_last_insert_rowid(db_);
        Statement member(db_, "INSERT INTO group_members (group_id, username) VALUES (?1,?2)");
        for (std::size_t i = 0; i < members.size(); ++i) {
            member.number(1, created);
            member.text(2, members[i]);
            member.step();
            member.reset();
        }
        Statement owner(db_, "INSERT INTO group_owners VALUES(?1,?2)");
        owner.number(1, created);
        owner.text(2, members.front());
        owner.step();
        transaction.commit();
        id = created;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool DirectoryStore::findGroup(int64_t id, GroupRow& result) {
    Statement group(db_, "SELECT name,revision FROM groups WHERE id=?1");
    group.number(1, id);
    if (!group.step()) return false;
    GroupRow row;
    row.id = id;
    row.name = group.columnText(0);
    row.revision = static_cast<uint64_t>(group.number(1));
    Statement members(db_, "SELECT username FROM group_members WHERE group_id=?1");
    members.number(1, id);
    row.members = readNames(members);
    result = row;
    return true;
}

// 派发快照时仍给已离群用户保留空成员的撤销记录。
std::vector<GroupRow> DirectoryStore::groupsForUser(const std::string& name) {
    Statement ids(db_,
        "SELECT group_id FROM group_members WHERE username=?1 UNION "
        "SELECT group_id FROM group_former_members WHERE username=?1 ORDER BY group_id LIMIT 128");
    ids.text(1, name);
    std::vector<GroupRow> result;
    while (ids.step()) {
        GroupRow group;
        if (!findGroup(ids.number(0), group)) continue;
        if (!contains(group.members, name)) group.members.clear();
        result.push_back(group);
    }
    return result;
}

std::string DirectoryStore::requestFriend(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return std::string();
    sqlite3_int64 sender = 0;
    sqlite3_int64 recipient = 0;
    if (!accountId(from, sender) || !accountId(to, recipient)) return std::string();
    if (contains(friendsForUser(from), to)) return "friends";
    const std::vector<std::string> pending = friendRequests(from, false);
    if (contains(pending, to)) return "pending";
    if (pending.size() >= 1024 || friendRequests(to, true).size() >= 1024)
        return std::string();
    Statement insert(db_, "INSERT OR IGNORE INTO friend_requests(sender,recipient) VALUES (?1,?2)");
    insert.number(1, sender);
    insert.number(2, recipient);
    insert.step();
    return "pending";
}

bool DirectoryStore::respondFriend(const std::string& recipient, const std::string& sender,
                                     bool accepted) {
    sqlite3_int64 to = 0;
    sqlite3_int64 from = 0;
    if (!accountId(recipient, to) || !accountId(sender, from) || to == from) return false;
    try {
        Transaction transaction(db_);
        {
            Statement pending(db_, "SELECT 1 FROM friend_requests WHERE sender=?1 AND recipient=?2");
            pending.number(1, from);
            pending.number(2, to);
            if (!pending.step()) return false;
        }
        if (accepted) {
            Statement insert(db_, "INSERT OR IGNORE INTO friends (user_id,friend_id) VALUES (?1,?2)");
            insert.number(1, to);
            insert.number(2, from);
            insert.step();
            insert.reset();
            insert.number(1, from);
            insert.number(2, to);
            insert.step();
        }
        Statement remove(db_, accepted ?
            "DELETE FROM friend_requests WHERE (sender=?1 AND recipient=?2) OR (sender=?2 AND recipient=?1)" :
            "DELETE FROM friend_requests WHERE sender=?1 AND recipient=?2");
        remove.number(1, from);
        remove.number(2, to);
        remove.step();
        transaction.commit();
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> DirectoryStore::friendRequests(const std::string& name, bool incoming) {
    sqlite3_int64 id = 0;
    if (!accountId(name, id)) return std::vector<std::string>();
    Statement query(db_, incoming ?
        "SELECT a.username FROM friend_requests r JOIN accounts a ON a.id=r.sender WHERE r.recipient=?1 ORDER BY a.username" :
        "SELECT a.username FROM friend_requests r JOIN accounts a ON a.id=r.recipient WHERE r.sender=?1 ORDER BY a.username");
    query.number(1, id);
    return readNames(query);
}

std::vector<std::string> DirectoryStore::friendsForUser(const std::string& name) {
    sqlite3_int64 id = 0;
    if (!accountId(name, id)) return std::vector<std::string>();
    Statement query(db_,
        "SELECT a.username FROM friends f JOIN accounts a ON a.id=f.friend_id WHERE f.user_id=?1 ORDER BY a.username");
    query.number(1, id);
    return readNames(query);
}

// 用用户名做翻页游标，保留第 51 行以判断是否还有下一页。
std::vector<std::string> DirectoryStore::searchUsernames(const std::string& name,
                                                         const std::string& value,
                                                         const std::string& after) {
    sqlite3_int64 id = 0;
    if (!accountId(name, id)) return std::vector<std::string>();
    Statement query(db_, "SELECT a.username FROM accounts a WHERE a.id<>?1 AND a.username>?2 "
        "AND instr(lower(a.username),lower(?3))>0 AND NOT EXISTS "
        "(SELECT 1 FROM friends f WHERE f.user_id=?1 AND f.friend_id=a.id) ORDER BY a.username LIMIT 51");
    query.number(1, id);
    query.text(2, after);
    query.text(3, value);
    return readNames(query);
}

void DirectoryStore::enqueueOffline(int64_t userId, const std::string& sender,
                                     const std::string& bytes) {
    Statement insert(db_, "INSERT INTO offline_messages (to_user, sender, payload) VALUES (?1,?2,?3)");
    insert.number(1, userId);
    insert.text(2, sender);
    insert.text(3, bytes);
    insert.step();
}

// v1 的旧队列在读取时移除；现行 v2 队列仍由 RelayStore 等待签收。
std::vector<OfflineRow> DirectoryStore::drainOffline(int64_t userId) {
    std::vector<OfflineRow> result;
    std::vector<sqlite3_int64> ids;
    Statement query(db_, "SELECT id,sender,payload FROM offline_messages WHERE to_user=?1 ORDER BY id");
    query.number(1, userId);
    while (query.step()) {
        ids.push_back(query.number(0));
        result.push_back(OfflineRow(query.columnText(1), query.columnText(2)));
    }
    query.reset();
    Statement remove(db_, "DELETE FROM offline_messages WHERE id=?1");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        remove.number(1, ids[i]);
        remove.step();
        remove.reset();
    }
    return result;
}

} // namespace chat
} // namespace nextalk
