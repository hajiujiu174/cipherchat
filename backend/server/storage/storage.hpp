#pragma once
// 服务器持久层（SQLite）：账号、预共享密钥、离线消息、群组。
// 聊天只存公钥束与密文；个人资料和好友可见朋友圈是服务器可读的社交数据，不存口令原文。
// 口令只存 PBKDF2-SHA256 哈希（登录验证用，非端到端体系的一部分）。

#include "common/base64.hpp"
#include "server/file_cache.hpp"
#include "ccfw/server_services.hpp"

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
    // 销毁服务端 SQLite 存储时释放它持有的资源。
    ~Storage();
    // 构造服务端 SQLite 存储，初始化后续操作需要的状态或依赖。
    Storage(const Storage&) = delete;
    // 将来源对象的状态赋值给当前对象。
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
    // 返回服务端 SQLite 存储使用的邮箱已注册。
    bool emailRegistered(const std::string& email);
    // 返回服务端 SQLite 存储使用的注册邮箱。
    std::string registrationEmail(const std::string& username);
    // 查找账号。
    std::optional<Account> findAccount(const std::string& username);
    // 查找账号By登录。
    std::optional<Account> findAccountByLogin(const std::string& usernameOrEmail);
    // 更新密码Auth。
    void updatePasswordAuth(const std::string& username,
                            const Bytes& passwordHash, const Bytes& salt,
                            std::int64_t iterations);

    // ---- 一次性预共享密钥 ----

    // ---- 离线消息队列 ----
    void enqueueOffline(std::int64_t toUserId, const std::string& sender,
                        const std::string& payloadJson);
    // 取走某用户全部离线消息（按入队顺序），并从库中删除
    struct OfflineMsg {
        std::string sender;
        std::string payloadJson;
    };
    // 取出离线。
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
    // 查找群聊。
    std::optional<Group> findGroup(std::int64_t groupId);
    // 返回服务端 SQLite 存储使用的群聊For用户。
    std::vector<Group> groupsForUser(const std::string& username);
    // 返回服务端 SQLite 存储使用的成员。
    ccfw::Result<std::vector<std::string>> members(std::int64_t groupId) override;

    // 全部用户名（演示规模全量返回，按注册顺序）
    std::vector<std::string> allUsernames();
    // 好友关系为双向服务器元数据；聊天密文仍不解密。
    std::string requestFriend(const std::string& from, const std::string& to);
    // 验证待处理申请，在同一事务内接受或拒绝；接受时写入双向好友关系。
    bool respondFriend(const std::string& recipient, const std::string& sender, bool accept);
    // 返回服务端 SQLite 存储使用的好友申请列表。
    std::vector<std::string> friendRequests(const std::string& username, bool incoming);
    // 返回服务端 SQLite 存储使用的好友For用户。
    std::vector<std::string> friendsForUser(const std::string& username);
    // 搜索用户名列表。
    std::vector<std::string> searchUsernames(const std::string& username, const std::string& query, const std::string& after = {});
    // 返回服务端 SQLite 存储使用的投递队列。
    ccfw::server::SqliteDeliveryQueue& deliveries() { return deliveries_; }
    // 返回服务端 SQLite 存储使用的服务端文件缓存。
    FileCache& fileCache() { return fileCache_; }
    // 按已认证的操作者分派资料、动态和社交数据操作。
    nlohmann::json socialRequest(const std::string& actor, const nlohmann::json& request, std::int64_t nowMs);
    // 判断Blocked条件是否成立。
    bool isBlocked(const std::string&, const std::string&);

private:
    // 返回服务端 SQLite 存储使用的extendedSocial请求。
    std::optional<nlohmann::json> extendedSocialRequest(const std::string&,const nlohmann::json&,std::int64_t);
    // 补充动态。
    void decorateMoment(nlohmann::json&);
    // 执行服务端 SQLite 存储。
    void exec(const char* sql);
    // 预编译语句的轻量包装
    class Stmt {
    public:
        // 构造数据存储，初始化后续操作需要的状态或依赖。
        Stmt(sqlite3* db, const char* sql);
        // 销毁数据存储时释放它持有的资源。
        ~Stmt();
        // 构造数据存储，初始化后续操作需要的状态或依赖。
        Stmt(const Stmt&) = delete;
        // 将来源对象的状态赋值给当前对象。
        Stmt& operator=(const Stmt&) = delete;
        // 绑定数据存储。
        void bind(int idx, const std::string& text);
        // 绑定数据存储。
        void bind(int idx, std::int64_t value);
        // step 到完成；返回 true 表示有行（仅查询用）
        bool step();
        // 返回数据存储使用的col文本。
        std::string colText(int idx);
        // 读取 SQLite 当前行指定列的二进制内容。
        Bytes colBlob(int idx);
        // 读取 SQLite 当前行指定列的整数值。
        std::int64_t colInt(int idx);
        // 重置数据存储。
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
