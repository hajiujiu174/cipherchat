#pragma once
// 服务器持久层（SQLite）：账号、预共享密钥、离线消息、群组。
// 聊天只存公钥束与密文；个人资料和好友可见朋友圈是服务器可读的社交数据，不存口令原文。
// 口令只存 PBKDF2-SHA256 哈希（登录验证用，非端到端体系的一部分）。

#include "common/base64.hpp"
#include "server/file_cache.hpp"
#include "framework/include/ccfw/server_services.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace cc::server {

class Storage : public ccfw::server::IGroupDirectory {
public:
    // 打开（必要时创建）数据库并建表。文件路径为空时用内存库（测试用）。
    explicit Storage(const std::string& path);
    ~Storage();
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // ---- 账号 ----
    struct Account {
        std::int64_t id = 0;
        std::string username;
        Bytes passwordHash;   // PBKDF2 输出
        Bytes salt;           // 登录挑战也复用注册时的盐
        std::int64_t iterations = 0;
        Bytes signPub;        // Ed25519 验签公钥（Base64 存原文串）
        Bytes dhPub;          // X25519 身份公钥
        Bytes signedPrekey;   // SPK 公钥
        Bytes spkSig;         // SPK 签名
    };
    // 注册；用户名已存在返回 nullopt
    std::optional<std::int64_t> createAccount(const std::string& username,
                                              const Bytes& passwordHash,
                                              const Bytes& salt,
                                              std::int64_t iterations,
                                              const std::string& signPub,
                                              const std::string& dhPub,
                                              const std::string& signedPrekey,
                                              const std::string& spkSig,
                                              const std::string& email = {});
    bool emailRegistered(const std::string& email);
    std::string registrationEmail(const std::string& username);
    std::optional<Account> findAccount(const std::string& username);
    std::optional<Account> findAccountByLogin(const std::string& usernameOrEmail);
    void updatePasswordAuth(const std::string& username,
                            const Bytes& passwordHash, const Bytes& salt,
                            std::int64_t iterations);

    // ---- 一次性预共享密钥 ----
    void addOneTimePrekeys(std::int64_t userId,
                           const std::vector<std::pair<std::int64_t, std::string>>& keys);
    // 弹出一个未用的一次性密钥（FIFO），用即删；无剩余返回 nullopt
    std::optional<std::pair<std::int64_t, std::string>> popOneTimePrekey(
        std::int64_t userId);
    std::int64_t countOneTimePrekeys(std::int64_t userId);

    // ---- 离线消息队列 ----
    void enqueueOffline(std::int64_t toUserId, const std::string& sender,
                        const std::string& payloadJson);
    // 取走某用户全部离线消息（按入队顺序），并从库中删除
    struct OfflineMsg {
        std::string sender;
        std::string payloadJson;
    };
    std::vector<OfflineMsg> drainOffline(std::int64_t userId);

    // ---- 群组 ----
    std::optional<std::int64_t> createGroup(const std::string& name,
                                            const std::vector<std::string>& members);
    struct Group {
        std::int64_t id = 0;
        std::string name;
        std::vector<std::string> members;
        std::uint64_t revision = 1;
    };
    std::optional<Group> findGroup(std::int64_t groupId);
    std::vector<Group> groupsForUser(const std::string& username);
    ccfw::Result<std::vector<std::string>> members(std::int64_t groupId) override;

    // 全部用户名（演示规模全量返回，按注册顺序）
    std::vector<std::string> allUsernames();
    // 好友关系为双向服务器元数据；聊天密文仍不解密。
    std::string requestFriend(const std::string& from, const std::string& to);
    bool respondFriend(const std::string& recipient, const std::string& sender, bool accept);
    std::vector<std::string> friendRequests(const std::string& username, bool incoming);
    std::vector<std::string> friendsForUser(const std::string& username);
    std::vector<std::string> searchUsernames(const std::string& username, const std::string& query, const std::string& after = {});
    ccfw::server::SqliteDeliveryQueue& deliveries() { return deliveries_; }
    FileCache& fileCache() { return fileCache_; }
    nlohmann::json socialRequest(const std::string& actor, const nlohmann::json& request, std::int64_t nowMs);

private:
    void exec(const char* sql);
    // 预编译语句的轻量包装
    class Stmt {
    public:
        Stmt(sqlite3* db, const char* sql);
        ~Stmt();
        Stmt(const Stmt&) = delete;
        Stmt& operator=(const Stmt&) = delete;
        void bind(int idx, const std::string& text);
        void bind(int idx, std::int64_t value);
        // step 到完成；返回 true 表示有行（仅查询用）
        bool step();
        std::string colText(int idx);
        Bytes colBlob(int idx);
        std::int64_t colInt(int idx);
        void reset();

    private:
        sqlite3_stmt* stmt_ = nullptr;
        sqlite3* db_ = nullptr;
    };

    sqlite3* db_ = nullptr;
    ccfw::server::SqliteDeliveryQueue deliveries_;
    FileCache fileCache_;
};

}  // namespace cc::server
