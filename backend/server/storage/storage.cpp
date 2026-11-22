#include "server/storage/storage.hpp"
#include "common/storage/sqlite_helpers.hpp"
#include "directory_store.hpp"
#include "server/profile_moments.hpp"
#include "server/storage/extended_schema.hpp"
#include "common/auth/email.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <set>
#include <algorithm>
#include <cctype>

namespace cc::server {

namespace {

// 检查数据库，交给调用方区分有效结果与错误。
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

// 构造服务端 SQLite 存储，初始化后续操作需要的状态或依赖。
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
    exec(extendedSchema);
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

// 销毁服务端 SQLite 存储时释放它持有的资源。
Storage::~Storage() {
    if (db_) sqlite3_close_v2(db_);
}

// 执行服务端 SQLite 存储。
void Storage::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("SQL 执行失败: " + msg);
    }
}

// ---- Stmt ----

// 构造数据存储，初始化后续操作需要的状态或依赖。
Storage::Stmt::Stmt(sqlite3* db, const char* sql) : db_(db) {
    checkDb(sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr), db_,
            "prepare");
}

// 销毁数据存储时释放它持有的资源。
Storage::Stmt::~Stmt() {
    if (stmt_) sqlite3_finalize(stmt_);
}

// 绑定数据存储。
void Storage::Stmt::bind(int idx, const std::string& text) {
    checkDb(sqlite3_bind_text(stmt_, idx, text.data(),
                              static_cast<int>(text.size()),
                              SQLITE_TRANSIENT), db_, "bind_text");
}

// 绑定数据存储。
void Storage::Stmt::bind(int idx, std::int64_t value) {
    checkDb(sqlite3_bind_int64(stmt_, idx, value), db_, "bind_int");
}

// 推进一步数据存储。
bool Storage::Stmt::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    if (rc == SQLITE_CONSTRAINT) return false;  // 如重名插入：调用方按失败处理
    checkDb(rc, db_, "step");
    return false;
}

// 返回数据存储使用的col文本。
std::string Storage::Stmt::colText(int idx) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, idx));
    const int n = sqlite3_column_bytes(stmt_, idx);
    return p ? std::string(p, static_cast<std::size_t>(n)) : std::string();
}

// 读取 SQLite 当前行指定列的二进制内容。
Bytes Storage::Stmt::colBlob(int idx) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_blob(stmt_, idx));
    const int n = sqlite3_column_bytes(stmt_, idx);
    return p ? Bytes(p, static_cast<std::size_t>(n)) : Bytes{};
}

// 读取 SQLite 当前行指定列的整数值。
std::int64_t Storage::Stmt::colInt(int idx) {
    return sqlite3_column_int64(stmt_, idx);
}

// 重置数据存储。
void Storage::Stmt::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ---- 账号 ----

// 在服务端存储用户账号与密码验证材料，并返回账号 ID。
std::optional<std::int64_t> Storage::createAccount(
    const std::string& username, const Bytes& passwordHash, const Bytes& salt,
    std::int64_t iterations, const std::string& signPub,
    const std::string& dhPub, const std::string& signedPrekey,
    const std::string& spkSig, const std::string& email) {
    exec("BEGIN IMMEDIATE");
    struct Rollback { sqlite3* db; bool committed = false; /* 销毁数据存储时释放它持有的资源。 */ ~Rollback() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); } } transaction{db_};
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

// 返回服务端 SQLite 存储使用的邮箱已注册。
bool Storage::emailRegistered(const std::string& email) {
    Stmt query(db_, "SELECT 1 FROM registration_emails WHERE email=?1"); query.bind(1,email); return query.step();
}
// 查找账号By登录。
std::optional<Storage::Account> Storage::findAccountByLogin(const std::string& input) {
    if (input.empty() || input.size()>254) return std::nullopt;
    if (auto account=findAccount(input)) return account;
    const auto email=cc::auth::normalizedEmail(input); if(email.empty()) return std::nullopt;
    std::string username;
    { Stmt query(db_,"SELECT accounts.username FROM accounts JOIN registration_emails ON accounts.id=user_id WHERE email=?1");
      query.bind(1,email); if(!query.step())return std::nullopt; username=query.colText(0); }
    return findAccount(username);
}
// 返回服务端 SQLite 存储使用的注册邮箱。
std::string Storage::registrationEmail(const std::string& username) {
    Stmt query(db_, "SELECT email FROM registration_emails JOIN accounts ON accounts.id=user_id WHERE username=?1");
    query.bind(1, username); return query.step() ? query.colText(0) : "";
}
// 查找账号。
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

// 更新密码Auth。
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

void Storage::enqueueOffline(std::int64_t toUserId, const std::string& sender,
                             const std::string& payloadJson) {
    nextalk::chat::DirectoryStore(db_).enqueueOffline(toUserId, sender, payloadJson);
}

std::vector<Storage::OfflineMsg> Storage::drainOffline(std::int64_t userId) {
    auto rows = nextalk::chat::DirectoryStore(db_).drainOffline(userId);
    std::vector<OfflineMsg> result;
    result.reserve(rows.size());
    for (auto& row : rows) result.push_back({std::move(row.sender), std::move(row.payload)});
    return result;
}

std::optional<std::int64_t> Storage::createGroup(
    const std::string& name, const std::vector<std::string>& members) {
    std::int64_t id = 0;
    if (!nextalk::chat::DirectoryStore(db_).createGroup(name, members, id)) return std::nullopt;
    return id;
}

std::optional<Storage::Group> Storage::findGroup(std::int64_t groupId) {
    nextalk::chat::GroupRow row;
    if (!nextalk::chat::DirectoryStore(db_).findGroup(groupId, row)) return std::nullopt;
    Group result;
    result.id = row.id;
    result.name = std::move(row.name);
    result.revision = row.revision;
    result.members = std::move(row.members);
    return result;
}

std::vector<Storage::Group> Storage::groupsForUser(const std::string& username) {
    auto rows = nextalk::chat::DirectoryStore(db_).groupsForUser(username);
    std::vector<Group> result;
    result.reserve(rows.size());
    for (auto& row : rows) {
        Group entry;
        entry.id = row.id;
        entry.name = std::move(row.name);
        entry.revision = row.revision;
        entry.members = std::move(row.members);
        result.push_back(std::move(entry));
    }
    return result;
}

ccfw::Result<std::vector<std::string>> Storage::members(std::int64_t id) {
    auto group = findGroup(id);
    if (!group)
        return ccfw::Result<std::vector<std::string>>::failure({ccfw::ErrorCode::NotFound, "M5-01", "群不存在"});
    return ccfw::Result<std::vector<std::string>>::success(std::move(group->members));
}

std::vector<std::string> Storage::allUsernames() {
    std::vector<std::string> result;
    Stmt query(db_, "SELECT username FROM accounts ORDER BY id");
    while (query.step()) result.push_back(query.colText(0));
    return result;
}

std::string Storage::requestFriend(const std::string& from, const std::string& to) {
    if (isBlocked(from, to)) return {};
    return nextalk::chat::DirectoryStore(db_).requestFriend(from, to);
}

std::vector<std::string> Storage::friendRequests(const std::string& username, bool incoming) {
    return nextalk::chat::DirectoryStore(db_).friendRequests(username, incoming);
}

bool Storage::respondFriend(const std::string& recipient, const std::string& sender, bool accept) {
    if (accept && isBlocked(recipient, sender)) return false;
    return nextalk::chat::DirectoryStore(db_).respondFriend(recipient, sender, accept);
}

std::vector<std::string> Storage::friendsForUser(const std::string& username) {
    return nextalk::chat::DirectoryStore(db_).friendsForUser(username);
}

std::vector<std::string> Storage::searchUsernames(const std::string& username, const std::string& query,
                                                  const std::string& after) {
    return nextalk::chat::DirectoryStore(db_).searchUsernames(username, query, after);
}

#include "extended_social.inc"

// 按已认证的操作者分派资料、动态和社交数据操作。
nlohmann::json Storage::socialRequest(const std::string& actor, const nlohmann::json& request, std::int64_t nowMs) {
    const auto account=findAccount(actor);if(!account)throw std::runtime_error("not_logged_in");
    if(auto extended=extendedSocialRequest(actor,request,nowMs))return *extended;
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
        for(auto& post:feed["posts"])decorateMoment(post);
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
        const auto image=request.value("image",std::string{});Bytes imagePng;
        if(!image.empty() && (image.size()>400*1024 || !base64Decode(image,imagePng)))throw std::runtime_error("invalid_avatar");
        validateProfilePng(imagePng);
        const auto key=request.at("request_id").get<std::string>();
        if(key.empty()||key.size()>64||!std::all_of(key.begin(),key.end(),[](unsigned char c){return std::isalnum(c)||c=='-';}))throw std::runtime_error("invalid_request");
        ccfw::sql::Transaction tx(db_);
        Stmt prior(db_,"SELECT id,text,created_ms FROM user_moments WHERE user_id=?1 AND request_id=?2");prior.bind(1,user->id);prior.bind(2,key);
        if(prior.step()) {
            if(prior.colText(1)!=text)throw std::runtime_error("conflict");
            Stmt stored(db_,"SELECT png FROM moment_images WHERE post_id=?1");stored.bind(1,prior.colInt(0));if((stored.step()?stored.colText(0):std::string{})!=image)throw std::runtime_error("conflict");
            result["id"]=prior.colInt(0);result["created_ms"]=prior.colInt(2);tx.commit();return result;
        }
        Stmt count(db_,"SELECT count(*),coalesce(max(created_ms),0) FROM user_moments WHERE user_id=?1");count.bind(1,user->id);count.step();
        if(count.colInt(0)>=10000)throw std::runtime_error("quota_reached");
        nowMs=std::max(nowMs,count.colInt(1)+1);
        Stmt add(db_,"INSERT INTO user_moments(user_id,request_id,text,created_ms) VALUES(?1,?2,?3,?4)");add.bind(1,user->id);add.bind(2,key);add.bind(3,text);add.bind(4,nowMs);add.step();
        const auto postId=sqlite3_last_insert_rowid(db_);
        if(!image.empty()){Stmt addImage(db_,"INSERT INTO moment_images VALUES(?1,?2)");addImage.bind(1,postId);addImage.bind(2,image);addImage.step();}
        result["id"]=postId;result["created_ms"]=nowMs;tx.commit();return result;
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
    for(auto& post:result["posts"])decorateMoment(post);
    return result;
}

}  // namespace cc::server
