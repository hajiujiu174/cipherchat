#include "ccfw/local_store.hpp"
#include "snapshot_codec.hpp"
#include "feature_codec.hpp"
#include "snapshot_store.hpp"
#include <set>
namespace ccfw {
namespace {
namespace classic = nextalk::chat;
// 返回local_store 模块使用的存储错误。
Error storageError(ErrorCode code = ErrorCode::StorageFailure) {
    return {code, "M3-03", "本地快照操作失败，原有数据已保留"};
}
// 返回local_store 模块使用的same账号范围。
bool sameScope(const AccountScope& a, const AccountScope& b) {
    return a.server == b.server && a.username == b.username;
}
constexpr const char* plainPrefix="CC-ORDINARY-1:";
// 封装local_store 模块。
cc::Bytes pack(cc::BytesView bytes) { return cc::Bytes(plainPrefix)+cc::Bytes(bytes); }
// 解包local_store 模块。
Result<cc::Bytes> unpack(ISecretProtector& legacy, cc::BytesView bytes) {
    if(bytes.starts_with(plainPrefix))return Result<cc::Bytes>::success(cc::Bytes(bytes.substr(std::char_traits<char>::length(plainPrefix))));
    return legacy.unseal(bytes); // Read-only importer for pre-migration snapshots.
}
// The first ordinary-chat release cleared old acknowledgement packets but kept
// completed encrypted transfers active. Recover ONLY their IDs from the original
// read-only archive; never restore archived keys or modify server receipts.
// 返回local_store 模块使用的旧版文件Ids。
std::set<std::string> legacyFileIds(const std::filesystem::path& path, ISecretProtector& protector, const AccountScope& scope) {
    auto archive=path;archive+=".before-ordinary.bak";
    if(!std::filesystem::is_regular_file(archive))return {};
    try {
        cc::Bytes contents;
        if (!classic::readArchivedSnapshot(codec::pathText(archive), contents)) return {};
        auto clear = unpack(protector, contents); if (!clear) return {};
        const auto json=nlohmann::json::from_cbor(clear.value());
        if(json.value("format",0)!=1 || !sameScope(json.at("account").get<AccountScope>(),scope))return {};
        std::set<std::string> ids;
        for(const auto& transfer:json.at("transfers")) {
            const auto id=transfer.at("id").get<std::string>();
            if(!id.empty() && id.size()<=32)ids.insert(id);
        }
        return ids;
    } catch(const std::exception&) {return {};}
}
}
struct SqliteLocalStore::Impl {
    sql::Database database;
    AccountScope scope;
    std::optional<ClientSnapshot> cached;
    cc::Bytes cachedCiphertext;
    cc::Bytes cachedFeatureCiphertext;
};
// 构造客户端 SQLite 存储，初始化后续操作需要的状态或依赖。
SqliteLocalStore::SqliteLocalStore(ISecretProtector& protector) : protector_(protector) {}
// 销毁客户端 SQLite 存储时释放它持有的资源。
SqliteLocalStore::~SqliteLocalStore() { close(); }
// 打开指定账号范围的本机数据库，初始化必要表结构及兼容状态。
Result<> SqliteLocalStore::open(const std::filesystem::path& path, const AccountScope& scope) {
    if (path.empty() || scope.server.empty() || scope.username.empty() || scope.username.size() > 32)
        return Result<>::failure(storageError(ErrorCode::InvalidArgument));
    try {
        const bool existed = std::filesystem::exists(path);
        if (existed && std::filesystem::file_size(path) == 0) return Result<>::failure(storageError());
        auto candidate = std::make_unique<Impl>();
        candidate->scope = scope;
        candidate->database.open(codec::pathText(path));
        const auto database = candidate->database.value;
        if (!existed) {
            ClientSnapshot initial;
            initial.account = scope;
            auto bytes = nlohmann::json::to_cbor(nlohmann::json(initial));
            classic::initializeSnapshot(database, scope.server, scope.username,
                pack(cc::BytesView(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
        }
        if (!classic::ensureSnapshotTables(database)) return Result<>::failure(storageError());
        // 候选数据库完整验证成功后才替换当前打开的账号。
        auto previous = std::move(impl_);
        impl_ = std::move(candidate);
        const auto validated = load();
        if (!validated) { impl_ = std::move(previous); return Result<>::failure(validated.error()); }
        if(existed && !impl_->cachedCiphertext.starts_with(plainPrefix)) {
            // SQLite backup includes WAL content; keep the original encrypted archive for rollback.
            auto backupPath=path;backupPath+=".before-ordinary.bak";
            if(std::filesystem::exists(backupPath))return Result<>::failure({ErrorCode::Conflict,"ordinary-chat","已有迁移备份，请保留备份并联系管理员核对"});
            sqlite3* backupDb=nullptr;
            if(sqlite3_open(codec::pathText(backupPath).c_str(),&backupDb)!=SQLITE_OK) { if(backupDb)sqlite3_close(backupDb); return Result<>::failure(storageError()); }
            auto backup=sqlite3_backup_init(backupDb,"main",database,"main");
            const auto copied=backup ? sqlite3_backup_step(backup,-1) : SQLITE_ERROR;
            if(backup)sqlite3_backup_finish(backup); sqlite3_close(backupDb);
            if(copied!=SQLITE_DONE)return Result<>::failure(storageError());
            auto clean=validated.value(); const auto revision=clean.revision++;
            auto migrated=commit(revision,clean); if(!migrated)return migrated;
        }
        const auto retired=legacyFileIds(path,protector_,scope);
        if(!retired.empty()) {
            auto current=load();if(!current)return Result<>::failure(current.error());
            auto clean=current.value();bool changed=false;
            for(auto& t:clean.transfers)if(retired.contains(t.fileId) && t.transportVersion==3) {
                t.transportVersion=2;t.fileKey.clear();t.previousTag.clear();t.pendingChunks.clear();t.lastAck.reset();if(!t.complete)t.cancelled=true;changed=true;
            }
            changed|=std::erase_if(clean.outbox,[&](const auto& p){return classic::legacyFilePacket(retired,p.kind,p.messageId);})>0;
            changed|=std::erase_if(clean.pendingSends,[&](const auto& p){return classic::legacyFilePacket(retired,p.kind,p.messageId);})>0;
            changed|=std::erase_if(clean.pendingPayloads,[&](const auto& p){return classic::legacyFilePacket(retired,p.kind,p.messageId);})>0;
            if(changed) {
                auto backupPath=path;backupPath+=".before-legacy-files.bak";
                for(unsigned suffix=1;std::filesystem::exists(backupPath);++suffix) {backupPath=path;backupPath+=".before-legacy-files-"+std::to_string(suffix)+".bak";}
                sqlite3* backupDb=nullptr;
                if(sqlite3_open(codec::pathText(backupPath).c_str(),&backupDb)!=SQLITE_OK) {if(backupDb)sqlite3_close(backupDb);return Result<>::failure(storageError());}
                auto backup=sqlite3_backup_init(backupDb,"main",database,"main");const auto copied=backup?sqlite3_backup_step(backup,-1):SQLITE_ERROR;
                if(backup)sqlite3_backup_finish(backup);sqlite3_close(backupDb);
                if(copied!=SQLITE_DONE)return Result<>::failure(storageError());
                const auto revision=clean.revision++;auto saved=commit(revision,clean);if(!saved)return saved;
            }
        }
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(storageError()); }
}
// 读取当前账号的本机快照和扩展数据，并按已有版本规则解码。
Result<ClientSnapshot> SqliteLocalStore::load() {
    if (!impl_) return Result<ClientSnapshot>::failure(storageError(ErrorCode::InvalidState));
    try {
        classic::SnapshotRow row;
        if (!classic::readSnapshot(impl_->database.value, impl_->scope.server, impl_->scope.username, row))
            return Result<ClientSnapshot>::failure(storageError());
        const auto& ciphertext = row.mainBytes;
        const auto& featureCiphertext = row.featureBytes;
        // Still read and compare the full stored ciphertext and scope/revision on every
        // load. External writes (including same-revision corruption) invalidate reuse.
        if (impl_->cached && impl_->cached->revision == row.revision && ciphertext == impl_->cachedCiphertext && featureCiphertext == impl_->cachedFeatureCiphertext)
            return Result<ClientSnapshot>::success(*impl_->cached);
        auto clear = unpack(protector_,ciphertext);
        if (!clear) return Result<ClientSnapshot>::failure(storageError());
        auto json = nlohmann::json::from_cbor(clear.value());
        auto snapshot = json.get<ClientSnapshot>();
        if (!sameScope(snapshot.account, impl_->scope) || snapshot.revision != row.revision)
            return Result<ClientSnapshot>::failure(storageError());
        if (!featureCiphertext.empty()) {
            auto clearFeatures = unpack(protector_,featureCiphertext); if (!clearFeatures) return Result<ClientSnapshot>::failure(storageError());
            features::decode(nlohmann::json::from_cbor(clearFeatures.value()), snapshot);
        }
        impl_->cached = snapshot; impl_->cachedCiphertext = ciphertext; impl_->cachedFeatureCiphertext = featureCiphertext;
        return Result<ClientSnapshot>::success(std::move(snapshot));
    } catch (const std::exception&) { return Result<ClientSnapshot>::failure(storageError()); }
}
// 核对账号范围及预期修订号，再用同一 SQLite 事务写入主快照和扩展功能状态。
Result<> SqliteLocalStore::commit(std::uint64_t expected, const ClientSnapshot& next) {
    if (!impl_) return Result<>::failure(storageError(ErrorCode::InvalidState));
    if (!classic::validSnapshotRevision(expected, next.revision) || !sameScope(next.account, impl_->scope))
        return Result<>::failure(storageError(ErrorCode::Conflict));
    try {
        const nlohmann::json json(next);
        (void)json.get<ClientSnapshot>(); // 写入前与读取路径用同一格式验证。
        const auto bytes = nlohmann::json::to_cbor(json);
        classic::SnapshotRow row;
        row.revision = next.revision;
        row.mainBytes = pack(cc::BytesView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        const auto featureJson = features::encode(next);
        const auto featureBytes = nlohmann::json::to_cbor(featureJson);
        row.featureBytes = pack(cc::BytesView(reinterpret_cast<const char*>(featureBytes.data()), featureBytes.size()));
        // Allocate the replacement before COMMIT; afterwards only nonthrowing
        // moves remain, so a committed write cannot be reported as a failure.
        ClientSnapshot replacement = next;
        features::decode(featureJson, replacement);
        const auto database = impl_->database.value;
        sql::Transaction transaction(database);
        const auto current = load();
        if (!current) return Result<>::failure(current.error());
        if (current.value().revision != expected) return Result<>::failure(storageError(ErrorCode::Conflict));
        if (!classic::updateSnapshot(database, impl_->scope.server, impl_->scope.username, expected, row))
            return Result<>::failure(storageError(ErrorCode::Conflict));
        transaction.commit();
        impl_->cached = std::move(replacement); impl_->cachedCiphertext = std::move(row.mainBytes); impl_->cachedFeatureCiphertext = std::move(row.featureBytes);
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(storageError()); }
}
// 关闭客户端 SQLite 存储。
void SqliteLocalStore::close() noexcept { impl_.reset(); }
}
