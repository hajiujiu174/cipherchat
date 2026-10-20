#include "server/storage/storage.hpp"
#include "framework/src/sqlite_helpers.hpp"
#include "server/profile_moments.hpp"
#include "common/auth/email.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <set>
#include <algorithm>
#include <cctype>

namespace cc::server {

namespace {

void checkDb(int rc, sqlite3* db, const char* what) {
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string(what) + ": " +
                                 sqlite3_errmsg(db));
    }
}

constexpr const char* kSchema = R"(
CREATE TABLE IF NOT EXISTS accounts (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    username     TEXT    NOT NULL UNIQUE,
    password_hash BLOB   NOT NULL,
    salt         BLOB    NOT NULL,
    iterations   INTEGER NOT NULL,
    sign_pub     TEXT    NOT NULL,
    dh_pub       TEXT    NOT NULL,
    signed_prekey TEXT   NOT NULL,
    spk_sig      TEXT    NOT NULL
);
CREATE TABLE IF NOT EXISTS registration_emails (
    user_id INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    email TEXT NOT NULL UNIQUE,
    verified_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);
CREATE TABLE IF NOT EXISTS one_time_prekeys (
    user_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    key_id   INTEGER NOT NULL,
    pub      TEXT    NOT NULL,
    PRIMARY KEY (user_id, key_id)
);
CREATE TABLE IF NOT EXISTS offline_messages (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    to_user   INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    sender    TEXT    NOT NULL,
    payload   TEXT    NOT NULL
);
CREATE TABLE IF NOT EXISTS groups (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision > 0)
);
CREATE TABLE IF NOT EXISTS group_members (
    group_id INTEGER NOT NULL REFERENCES groups(id) ON DELETE CASCADE,
    username TEXT NOT NULL,
    PRIMARY KEY (group_id, username)
);
CREATE TABLE IF NOT EXISTS friends (
    user_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    friend_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    PRIMARY KEY (user_id, friend_id),
    CHECK (user_id <> friend_id)
);
CREATE TABLE IF NOT EXISTS friend_requests (
    sender INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    recipient INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (sender, recipient), CHECK (sender <> recipient)
);
CREATE INDEX IF NOT EXISTS requests_recipient ON friend_requests(recipient);
CREATE TABLE IF NOT EXISTS user_profiles (
 user_id INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
 nickname TEXT NOT NULL, avatar_png BLOB NOT NULL);
CREATE TABLE IF NOT EXISTS user_moments (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 user_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 request_id TEXT NOT NULL, text TEXT NOT NULL, created_ms INTEGER NOT NULL,
 UNIQUE(user_id,request_id));
CREATE INDEX IF NOT EXISTS user_moments_page ON user_moments(user_id,id DESC);
CREATE INDEX IF NOT EXISTS user_moments_feed ON user_moments(created_ms DESC,id DESC);

)";

}  // namespace

Storage::Storage(const std::string& path) {
    const int rc =
        sqlite3_open_v2(path.empty() ? ":memory:" : path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("无法打开数据库: " +
                                 std::string(db_ ? sqlite3_errmsg(db_) : "?"));
    }
    checkDb(sqlite3_busy_timeout(db_, 3000), db_, "busy_timeout");
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec(kSchema);
    fileCache_.open(path.empty() ? "" : path+".files.sqlite3");
    bool revisionExists = false;
    {
        Stmt columns(db_, "PRAGMA table_info(groups)");
        while (columns.step()) if (columns.colText(1) == "revision") revisionExists = true;
    }
    if (!revisionExists) exec("ALTER TABLE groups ADD COLUMN revision INTEGER NOT NULL DEFAULT 1 CHECK(revision > 0)");
    if (!deliveries_.open(path)) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("无法打开可靠密文队列");
    }
}

Storage::~Storage() {
    if (db_) sqlite3_close_v2(db_);
}

void Storage::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("SQL 执行失败: " + msg);
    }
}

// ---- Stmt ----

Storage::Stmt::Stmt(sqlite3* db, const char* sql) : db_(db) {
    checkDb(sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr), db_,
            "prepare");
}

Storage::Stmt::~Stmt() {
    if (stmt_) sqlite3_finalize(stmt_);
}

void Storage::Stmt::bind(int idx, const std::string& text) {
    checkDb(sqlite3_bind_text(stmt_, idx, text.data(),
                              static_cast<int>(text.size()),
                              SQLITE_TRANSIENT), db_, "bind_text");
}

void Storage::Stmt::bind(int idx, std::int64_t value) {
    checkDb(sqlite3_bind_int64(stmt_, idx, value), db_, "bind_int");
}

bool Storage::Stmt::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    if (rc == SQLITE_CONSTRAINT) return false;  // 如重名插入：调用方按失败处理
    checkDb(rc, db_, "step");
    return false;
}

std::string Storage::Stmt::colText(int idx) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, idx));
    const int n = sqlite3_column_bytes(stmt_, idx);
    return p ? std::string(p, static_cast<std::size_t>(n)) : std::string();
}

Bytes Storage::Stmt::colBlob(int idx) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_blob(stmt_, idx));
    const int n = sqlite3_column_bytes(stmt_, idx);
    return p ? Bytes(p, static_cast<std::size_t>(n)) : Bytes{};
}

std::int64_t Storage::Stmt::colInt(int idx) {
    return sqlite3_column_int64(stmt_, idx);
}

void Storage::Stmt::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ---- 账号 ----

std::optional<std::int64_t> Storage::createAccount(
    const std::string& username, const Bytes& passwordHash, const Bytes& salt,
    std::int64_t iterations, const std::string& signPub,
    const std::string& dhPub, const std::string& signedPrekey,
    const std::string& spkSig, const std::string& email) {
    exec("BEGIN IMMEDIATE");
    struct Rollback { sqlite3* db; bool committed = false; ~Rollback() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); } } transaction{db_};
    // RETURNING id：插入成功返回新 id，重名约束失败则无行
    Stmt st(db_,
            "INSERT INTO accounts (username, password_hash, salt, iterations,"
            " sign_pub, dh_pub, signed_prekey, spk_sig)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7,?8)"
            " RETURNING id");
    st.bind(1, username);
    st.bind(2, passwordHash);
    st.bind(3, salt);
    st.bind(4, iterations);
    st.bind(5, signPub);
    st.bind(6, dhPub);
    st.bind(7, signedPrekey);
    st.bind(8, spkSig);
    if (!st.step()) return std::nullopt;
    const auto id = st.colInt(0);
    st.step(); // Finish RETURNING before COMMIT.
    if (!email.empty()) {
        Stmt bindEmail(db_, "INSERT INTO registration_emails(user_id,email) VALUES(?1,?2) RETURNING user_id");
        bindEmail.bind(1, id); bindEmail.bind(2, email);
        if (!bindEmail.step()) return std::nullopt;
        bindEmail.step();
    }
    exec("COMMIT"); transaction.committed = true;
    return id;
}

bool Storage::emailRegistered(const std::string& email) {
    Stmt query(db_, "SELECT 1 FROM registration_emails WHERE email=?1"); query.bind(1,email); return query.step();
}
std::optional<Storage::Account> Storage::findAccountByLogin(const std::string& input) {
    if (input.empty() || input.size()>254) return std::nullopt;
    if (auto account=findAccount(input)) return account;
    const auto email=cc::auth::normalizedEmail(input); if(email.empty()) return std::nullopt;
    std::string username;
    { Stmt query(db_,"SELECT accounts.username FROM accounts JOIN registration_emails ON accounts.id=user_id WHERE email=?1");
      query.bind(1,email); if(!query.step())return std::nullopt; username=query.colText(0); }
    return findAccount(username);
}
std::string Storage::registrationEmail(const std::string& username) {
    Stmt query(db_, "SELECT email FROM registration_emails JOIN accounts ON accounts.id=user_id WHERE username=?1");
    query.bind(1, username); return query.step() ? query.colText(0) : "";
}
std::optional<Storage::Account> Storage::findAccount(
    const std::string& username) {
    Stmt st(db_,
            "SELECT id, username, password_hash, salt, iterations, sign_pub,"
            " dh_pub, signed_prekey, spk_sig FROM accounts WHERE username=?1");
    st.bind(1, username);
    if (!st.step()) return std::nullopt;
    Account a;
    a.id = st.colInt(0);
    a.username = st.colText(1);
    a.passwordHash = st.colBlob(2);
    a.salt = st.colBlob(3);
    a.iterations = st.colInt(4);
    a.signPub = st.colText(5);
    a.dhPub = st.colText(6);
    a.signedPrekey = st.colText(7);
    a.spkSig = st.colText(8);
    return a;
}

void Storage::updatePasswordAuth(const std::string& username,
                                 const Bytes& passwordHash, const Bytes& salt,
                                 std::int64_t iterations) {
    Stmt st(db_,
            "UPDATE accounts SET password_hash=?2, salt=?3, iterations=?4"
            " WHERE username=?1");
    st.bind(1, username);
    st.bind(2, passwordHash);
    st.bind(3, salt);
    st.bind(4, iterations);
    st.step();
}

// ---- 一次性预共享密钥 ----

void Storage::addOneTimePrekeys(
    std::int64_t userId,
    const std::vector<std::pair<std::int64_t, std::string>>& keys) {
    Stmt st(db_,
            "INSERT OR REPLACE INTO one_time_prekeys (user_id, key_id, pub)"
            " VALUES (?1,?2,?3)");
    for (const auto& [id, pub] : keys) {
        st.bind(1, userId);
        st.bind(2, id);
        st.bind(3, pub);
        st.step();
        st.reset();
    }
}

std::optional<std::pair<std::int64_t, std::string>>
Storage::popOneTimePrekey(std::int64_t userId) {
    // FIFO：按 key_id 顺序弹出最旧的一条并删除
    Stmt sel(db_,
             "SELECT key_id, pub FROM one_time_prekeys WHERE user_id=?1"
             " ORDER BY key_id LIMIT 1");
    sel.bind(1, userId);
    if (!sel.step()) return std::nullopt;
    const std::int64_t id = sel.colInt(0);
    const std::string pub = sel.colText(1);
    Stmt del(db_, "DELETE FROM one_time_prekeys WHERE user_id=?1 AND key_id=?2");
    del.bind(1, userId);
    del.bind(2, id);
    del.step();
    return std::make_pair(id, pub);
}

std::int64_t Storage::countOneTimePrekeys(std::int64_t userId) {
    Stmt st(db_, "SELECT COUNT(*) FROM one_time_prekeys WHERE user_id=?1");
    st.bind(1, userId);
    return st.step() ? st.colInt(0) : 0;
}

// ---- 离线消息 ----

void Storage::enqueueOffline(std::int64_t toUserId, const std::string& sender,
                             const std::string& payloadJson) {
    Stmt st(db_,
            "INSERT INTO offline_messages (to_user, sender, payload)"
            " VALUES (?1,?2,?3)");
    st.bind(1, toUserId);
    st.bind(2, sender);
    st.bind(3, payloadJson);
    st.step();
}

std::vector<Storage::OfflineMsg> Storage::drainOffline(std::int64_t userId) {
    std::vector<OfflineMsg> out;
    Stmt sel(db_,
             "SELECT id, sender, payload FROM offline_messages"
             " WHERE to_user=?1 ORDER BY id");
    {
        sel.bind(1, userId);
        std::vector<std::int64_t> ids;
        while (sel.step()) {
            ids.push_back(sel.colInt(0));
            out.push_back({sel.colText(1), sel.colText(2)});
        }
        sel.reset();
        Stmt del(db_, "DELETE FROM offline_messages WHERE id=?1");
        for (const std::int64_t id : ids) {
            del.bind(1, id);
            del.step();
            del.reset();
        }
    }
    return out;
}

// ---- 群组 ----

std::optional<std::int64_t> Storage::createGroup(
    const std::string& name, const std::vector<std::string>& members) {
    if (name.empty() || name.size() > 128 || members.size() < 2 || members.size() > 64 ||
        std::set<std::string>(members.begin(), members.end()).size() != members.size()) return std::nullopt;
    for (const auto& member : members) if (!findAccount(member) || groupsForUser(member).size() >= 128) return std::nullopt;
    try {
        ccfw::sql::Transaction transaction(db_);
        ccfw::sql::Statement ins(db_, "INSERT INTO groups (name) VALUES (?1)");
        ins.text(1, name);
        ins.step();
        const std::int64_t gid = sqlite3_last_insert_rowid(db_);
        for (const auto& m : members) {
            ccfw::sql::Statement mem(db_, "INSERT INTO group_members (group_id, username) VALUES (?1,?2)");
            mem.number(1, gid);
            mem.text(2, m);
            mem.step();
        }
        transaction.commit();
        return gid;
    } catch (const std::exception&) { return std::nullopt; }
}

std::optional<Storage::Group> Storage::findGroup(std::int64_t groupId) {
    Stmt g(db_, "SELECT name,revision FROM groups WHERE id=?1");
    g.bind(1, groupId);
    if (!g.step()) return std::nullopt;
    Group grp;
    grp.id = groupId;
    grp.name = g.colText(0);
    grp.revision = static_cast<std::uint64_t>(g.colInt(1));
    Stmt mem(db_, "SELECT username FROM group_members WHERE group_id=?1");
    mem.bind(1, groupId);
    while (mem.step()) grp.members.push_back(mem.colText(0));
    return grp;
}

std::vector<Storage::Group> Storage::groupsForUser(const std::string& username) {
    Stmt ids(db_, "SELECT group_id FROM group_members WHERE username=?1 ORDER BY group_id LIMIT 128");
    ids.bind(1, username); std::vector<Group> result;
    while (ids.step()) if (auto group = findGroup(ids.colInt(0))) result.push_back(*group);
    return result;
}
ccfw::Result<std::vector<std::string>> Storage::members(std::int64_t id) {
    auto group = findGroup(id);
    if (!group) return ccfw::Result<std::vector<std::string>>::failure({ccfw::ErrorCode::NotFound, "M5-01", "群不存在"});
    return ccfw::Result<std::vector<std::string>>::success(group->members);
}

std::vector<std::string> Storage::allUsernames() {
    std::vector<std::string> out;
    Stmt st(db_, "SELECT username FROM accounts ORDER BY id");
    while (st.step()) out.push_back(st.colText(0));
    return out;
}

std::string Storage::requestFriend(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return {};
    const auto a = findAccount(from), b = findAccount(to);
    if (!a || !b) return {};
    const auto friends = friendsForUser(from);
    if (std::find(friends.begin(), friends.end(), to) != friends.end()) return "friends";
    const auto pending = friendRequests(from, false);
    if (std::find(pending.begin(), pending.end(), to) != pending.end()) return "pending";
    if (pending.size() >= 1024 || friendRequests(to, true).size() >= 1024) return {};
    Stmt st(db_, "INSERT OR IGNORE INTO friend_requests(sender,recipient) VALUES (?1,?2)");
    st.bind(1, a->id); st.bind(2, b->id); st.step();
    return "pending";
}

std::vector<std::string> Storage::friendRequests(const std::string& username, bool incoming) {
    std::vector<std::string> result; const auto account = findAccount(username); if (!account) return result;
    Stmt st(db_, incoming ?
        "SELECT a.username FROM friend_requests r JOIN accounts a ON a.id=r.sender WHERE r.recipient=?1 ORDER BY a.username" :
        "SELECT a.username FROM friend_requests r JOIN accounts a ON a.id=r.recipient WHERE r.sender=?1 ORDER BY a.username");
    st.bind(1, account->id); while (st.step()) result.push_back(st.colText(0)); return result;
}

bool Storage::respondFriend(const std::string& recipient, const std::string& sender, bool accept) {
    const auto a = findAccount(recipient), b = findAccount(sender);
    if (!a || !b || a->id == b->id) return false;
    try {
        ccfw::sql::Transaction transaction(db_);
        {
            Stmt pending(db_, "SELECT 1 FROM friend_requests WHERE sender=?1 AND recipient=?2");
            pending.bind(1, b->id); pending.bind(2, a->id); if (!pending.step()) return false;
        }
        if (accept) {
        ccfw::sql::Statement insert(db_, "INSERT OR IGNORE INTO friends (user_id,friend_id) VALUES (?1,?2)");
        insert.number(1, a->id); insert.number(2, b->id); insert.step();
        ccfw::sql::Statement reverse(db_, "INSERT OR IGNORE INTO friends (user_id,friend_id) VALUES (?1,?2)");
        reverse.number(1, b->id); reverse.number(2, a->id); reverse.step();
        }
        Stmt remove(db_, accept ?
            "DELETE FROM friend_requests WHERE (sender=?1 AND recipient=?2) OR (sender=?2 AND recipient=?1)" :
            "DELETE FROM friend_requests WHERE sender=?1 AND recipient=?2");
        remove.bind(1, b->id); remove.bind(2, a->id); remove.step();
        transaction.commit(); return true;
    } catch (...) { return false; }
}

std::vector<std::string> Storage::friendsForUser(const std::string& username) {
    std::vector<std::string> result; const auto account = findAccount(username); if (!account) return result;
    Stmt st(db_, "SELECT a.username FROM friends f JOIN accounts a ON a.id=f.friend_id WHERE f.user_id=?1 ORDER BY a.username");
    st.bind(1, account->id); while (st.step()) result.push_back(st.colText(0)); return result;
}

std::vector<std::string> Storage::searchUsernames(const std::string& username, const std::string& query, const std::string& after) {
    std::vector<std::string> result; const auto account = findAccount(username); if (!account) return result;
    // Keyset pagination: 50 rows plus one lookahead; query is literal, not a LIKE pattern.
    Stmt st(db_, "SELECT a.username FROM accounts a WHERE a.id<>?1 AND a.username>?2 "
        "AND instr(lower(a.username),lower(?3))>0 AND NOT EXISTS "
        "(SELECT 1 FROM friends f WHERE f.user_id=?1 AND f.friend_id=a.id) ORDER BY a.username LIMIT 51");
    st.bind(1, account->id); st.bind(2, after); st.bind(3, query);
    while (st.step()) result.push_back(st.colText(0));
    return result;
}

nlohmann::json Storage::socialRequest(const std::string& actor, const nlohmann::json& request, std::int64_t nowMs) {
    const auto account=findAccount(actor);if(!account)throw std::runtime_error("not_logged_in");
    const auto op=request.at("op").get<std::string>();
    if(op=="feed") {
        const auto beforeMs=request.value("before_ms",std::int64_t{0});
        const auto beforeId=request.value("before_id",std::int64_t{0});
        if(beforeMs<0||beforeId<0||(beforeMs==0)!=(beforeId==0))throw std::runtime_error("invalid_cursor");
        nlohmann::json feed={{"ok",true},{"op",op},{"username",actor},
            {"view_id",request.value("view_id",std::string{})},{"before_ms",beforeMs},{"before_id",beforeId},
            {"posts",nlohmann::json::array()},{"next_ms",0},{"next_id",0}};
        // Authentication determines the audience; client-supplied usernames cannot broaden it.
        Stmt page(db_,"SELECT m.id,m.text,m.created_ms,a.username,coalesce(p.nickname,a.username) "
            "FROM user_moments m JOIN accounts a ON a.id=m.user_id LEFT JOIN user_profiles p ON p.user_id=a.id "
            "WHERE (m.user_id=?1 OR EXISTS(SELECT 1 FROM friends f WHERE f.user_id=?1 AND f.friend_id=m.user_id)) "
            "AND (?2=0 OR m.created_ms<?2 OR (m.created_ms=?2 AND m.id<?3)) "
            "ORDER BY m.created_ms DESC,m.id DESC LIMIT 21");
        page.bind(1,account->id);page.bind(2,beforeMs);page.bind(3,beforeId);
        while(page.step()) {
            if(feed["posts"].size()==20) {
                feed["next_ms"]=feed["posts"].back()["created_ms"];
                feed["next_id"]=feed["posts"].back()["id"];break;
            }
            feed["posts"].push_back({{"id",page.colInt(0)},{"text",page.colText(1)},
                {"created_ms",page.colInt(2)},{"username",page.colText(3)},{"nickname",page.colText(4)}});
        }
        return feed;
    }
    const auto target=request.value("username",actor);
    if(target.empty()||target.size()>32)throw std::runtime_error("invalid_user");
    auto user=findAccount(target);if(!user)throw std::runtime_error("not_found");
    nlohmann::json result={{"ok",true},{"op",op},{"username",target},{"view_id",request.value("view_id",std::string{})}};
    if(op=="save") {
        if(target!=actor)throw std::runtime_error("forbidden");
        const auto nickname=request.at("nickname").get<std::string>();validateSocialText(nickname,32,false);
        const auto encoded=request.at("avatar").get<std::string>();Bytes png;
        if(encoded.size()>400*1024||!cc::base64Decode(encoded,png))throw std::runtime_error("invalid_avatar");
        validateProfilePng(png);
        ccfw::sql::Statement write(db_,"INSERT INTO user_profiles(user_id,nickname,avatar_png) VALUES(?1,?2,?3) ON CONFLICT(user_id) DO UPDATE SET nickname=excluded.nickname,avatar_png=excluded.avatar_png");
        write.number(1,user->id);write.text(2,nickname);write.blob(3,png);write.step();
        result["nickname"]=nickname;result["avatar"]=encoded;return result;
    }
    if(op=="publish") {
        if(target!=actor)throw std::runtime_error("forbidden");
        const auto text=request.at("text").get<std::string>();validateSocialText(text,2000,true);
        const auto key=request.at("request_id").get<std::string>();
        if(key.empty()||key.size()>64||!std::all_of(key.begin(),key.end(),[](unsigned char c){return std::isalnum(c)||c=='-';}))throw std::runtime_error("invalid_request");
        ccfw::sql::Transaction tx(db_);
        Stmt prior(db_,"SELECT id,text,created_ms FROM user_moments WHERE user_id=?1 AND request_id=?2");prior.bind(1,user->id);prior.bind(2,key);
        if(prior.step()) {
            if(prior.colText(1)!=text)throw std::runtime_error("conflict");
            result["id"]=prior.colInt(0);result["created_ms"]=prior.colInt(2);tx.commit();return result;
        }
        Stmt count(db_,"SELECT count(*),coalesce(max(created_ms),0) FROM user_moments WHERE user_id=?1");count.bind(1,user->id);count.step();
        if(count.colInt(0)>=10000)throw std::runtime_error("quota_reached");
        nowMs=std::max(nowMs,count.colInt(1)+1);
        Stmt add(db_,"INSERT INTO user_moments(user_id,request_id,text,created_ms) VALUES(?1,?2,?3,?4)");add.bind(1,user->id);add.bind(2,key);add.bind(3,text);add.bind(4,nowMs);add.step();
        result["id"]=sqlite3_last_insert_rowid(db_);result["created_ms"]=nowMs;tx.commit();return result;
    }
    if(op!="get")throw std::runtime_error("invalid_request");
    result["nickname"]=target;result["avatar"]="";
    {Stmt profile(db_,"SELECT nickname,avatar_png FROM user_profiles WHERE user_id=?1");profile.bind(1,user->id);if(profile.step()){result["nickname"]=profile.colText(0);result["avatar"]=cc::base64Encode(profile.colBlob(1));}}
    const auto friends=friendsForUser(actor);const bool allowed=actor==target||std::find(friends.begin(),friends.end(),target)!=friends.end();
    result["can_view"]=allowed;result["posts"]=nlohmann::json::array();result["next"]=0;
    const auto before=request.value("before",std::int64_t{0});if(before<0)throw std::runtime_error("invalid_cursor");result["before"]=before;
    if(allowed) {
        Stmt page(db_,"SELECT id,text,created_ms FROM user_moments WHERE user_id=?1 AND (?2=0 OR id<?2) ORDER BY id DESC LIMIT 21");page.bind(1,user->id);page.bind(2,before);
        while(page.step()){
            if(result["posts"].size()==20){result["next"]=result["posts"].back()["id"];break;}
            result["posts"].push_back({{"id",page.colInt(0)},{"text",page.colText(1)},{"created_ms",page.colInt(2)}});
        }
    }
    return result;
}

}  // namespace cc::server
