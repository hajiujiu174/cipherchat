#include "ccfw/local_store.hpp"
#include "snapshot_codec.hpp"
#include "feature_codec.hpp"
#include "sqlite_helpers.hpp"
namespace ccfw {
namespace {
Error storageError(ErrorCode code = ErrorCode::StorageFailure) {
    return {code, "M3-03", "本地快照操作失败，原有数据已保留"};
}
bool sameScope(const AccountScope& a, const AccountScope& b) {
    return a.server == b.server && a.username == b.username;
}
constexpr const char* plainPrefix="CC-ORDINARY-1:";
cc::Bytes pack(cc::BytesView bytes) { return cc::Bytes(plainPrefix)+cc::Bytes(bytes); }
Result<cc::Bytes> unpack(ISecretProtector& legacy, cc::BytesView bytes) {
    if(bytes.starts_with(plainPrefix))return Result<cc::Bytes>::success(cc::Bytes(bytes.substr(std::char_traits<char>::length(plainPrefix))));
    return legacy.unseal(bytes); // Read-only importer for pre-migration snapshots.
}
constexpr const char* schema = R"(
CREATE TABLE client_state (
 singleton INTEGER PRIMARY KEY CHECK(singleton=1), server_id TEXT NOT NULL,
 username TEXT NOT NULL, revision INTEGER NOT NULL CHECK(revision>=0),
 format_version INTEGER NOT NULL CHECK(format_version=1), protected_snapshot BLOB NOT NULL);
PRAGMA user_version=1;
)";
}
struct SqliteLocalStore::Impl {
    sql::Database database;
    AccountScope scope;
    std::optional<ClientSnapshot> cached;
    cc::Bytes cachedCiphertext;
    cc::Bytes cachedFeatureCiphertext;
};
SqliteLocalStore::SqliteLocalStore(ISecretProtector& protector) : protector_(protector) {}
SqliteLocalStore::~SqliteLocalStore() { close(); }
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
            auto protectedData = Result<cc::Bytes>::success(pack(cc::BytesView(reinterpret_cast<char*>(bytes.data()), bytes.size())));
            if (!protectedData) return Result<>::failure(storageError());
            sql::Transaction transaction(database);
            sql::exec(database, schema);
            sql::Statement insert(database, "INSERT INTO client_state VALUES(1,?1,?2,0,1,?3)");
            insert.text(1, scope.server); insert.text(2, scope.username); insert.blob(3, protectedData.value());
            insert.step(); transaction.commit();
        }
        sql::Statement version(database, "PRAGMA user_version");
        if (!version.step() || version.number(0) != 1) return Result<>::failure(storageError());
        sql::exec(database, "CREATE TABLE IF NOT EXISTS client_features (singleton INTEGER PRIMARY KEY CHECK(singleton=1), format_version INTEGER NOT NULL CHECK(format_version=1), protected_features BLOB NOT NULL)");
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
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(storageError()); }
}
Result<ClientSnapshot> SqliteLocalStore::load() {
    if (!impl_) return Result<ClientSnapshot>::failure(storageError(ErrorCode::InvalidState));
    try {
        sql::Statement row(impl_->database.value, "SELECT server_id,username,revision,format_version,protected_snapshot FROM client_state WHERE singleton=1");
        if (!row.step() || row.bytes(0) != impl_->scope.server || row.bytes(1) != impl_->scope.username ||
            row.number(2) < 0 || row.number(3) != 1) return Result<ClientSnapshot>::failure(storageError());
        const auto ciphertext = row.bytes(4);
        cc::Bytes featureCiphertext;
        sql::Statement feature(impl_->database.value, "SELECT format_version,protected_features FROM client_features WHERE singleton=1");
        if (feature.step()) { if (feature.number(0) != 1) return Result<ClientSnapshot>::failure(storageError()); featureCiphertext = feature.bytes(1); if (featureCiphertext.empty()) return Result<ClientSnapshot>::failure(storageError()); }
        // Still read and compare the full stored ciphertext and scope/revision on every
        // load. External writes (including same-revision corruption) invalidate reuse.
        if (impl_->cached && impl_->cached->revision == static_cast<std::uint64_t>(row.number(2)) && ciphertext == impl_->cachedCiphertext && featureCiphertext == impl_->cachedFeatureCiphertext)
            return Result<ClientSnapshot>::success(*impl_->cached);
        auto clear = unpack(protector_,ciphertext);
        if (!clear) return Result<ClientSnapshot>::failure(storageError());
        auto json = nlohmann::json::from_cbor(clear.value());
        auto snapshot = json.get<ClientSnapshot>();
        if (!sameScope(snapshot.account, impl_->scope) || snapshot.revision != static_cast<std::uint64_t>(row.number(2)))
            return Result<ClientSnapshot>::failure(storageError());
        if (!featureCiphertext.empty()) {
            auto clearFeatures = unpack(protector_,featureCiphertext); if (!clearFeatures) return Result<ClientSnapshot>::failure(storageError());
            features::decode(nlohmann::json::from_cbor(clearFeatures.value()), snapshot);
        }
        impl_->cached = snapshot; impl_->cachedCiphertext = ciphertext; impl_->cachedFeatureCiphertext = featureCiphertext;
        return Result<ClientSnapshot>::success(std::move(snapshot));
    } catch (const std::exception&) { return Result<ClientSnapshot>::failure(storageError()); }
}
Result<> SqliteLocalStore::commit(std::uint64_t expected, const ClientSnapshot& next) {
    if (!impl_) return Result<>::failure(storageError(ErrorCode::InvalidState));
    if (expected >= INT64_MAX || next.revision != expected + 1 || !sameScope(next.account, impl_->scope))
        return Result<>::failure(storageError(ErrorCode::Conflict));
    try {
        const nlohmann::json json(next);
        (void)json.get<ClientSnapshot>(); // 写入前与读取路径用同一格式验证。
        const auto bytes = nlohmann::json::to_cbor(json);
        auto sealed = Result<cc::Bytes>::success(pack(cc::BytesView(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
        if (!sealed) return Result<>::failure(storageError());
        const auto featureJson = features::encode(next);
        ClientSnapshot check = next; features::decode(featureJson, check);
        const auto featureBytes = nlohmann::json::to_cbor(featureJson);
        auto featureSealed = Result<cc::Bytes>::success(pack(cc::BytesView(reinterpret_cast<const char*>(featureBytes.data()), featureBytes.size())));
        if (!featureSealed) return Result<>::failure(storageError());
        // Allocate the replacement before COMMIT; afterwards only nonthrowing
        // moves remain, so a committed write cannot be reported as a failure.
        ClientSnapshot replacement = next;
        features::decode(featureJson, replacement);
        const auto database = impl_->database.value;
        sql::Transaction transaction(database);
        const auto current = load();
        if (!current) return Result<>::failure(current.error());
        if (current.value().revision != expected) return Result<>::failure(storageError(ErrorCode::Conflict));
        sql::Statement update(database,
            "UPDATE client_state SET revision=?1,protected_snapshot=?2 WHERE singleton=1 AND revision=?3 AND server_id=?4 AND username=?5");
        update.number(1, static_cast<std::int64_t>(next.revision)); update.blob(2, sealed.value());
        update.number(3, static_cast<std::int64_t>(expected)); update.text(4, impl_->scope.server); update.text(5, impl_->scope.username);
        update.step();
        if (sqlite3_changes(database) != 1) return Result<>::failure(storageError(ErrorCode::Conflict));
        sql::Statement featureUpdate(database, "INSERT INTO client_features VALUES(1,1,?1) ON CONFLICT(singleton) DO UPDATE SET protected_features=excluded.protected_features");
        featureUpdate.blob(1, featureSealed.value()); featureUpdate.step();
        transaction.commit();
        impl_->cached = std::move(replacement); impl_->cachedCiphertext = std::move(sealed.value()); impl_->cachedFeatureCiphertext = std::move(featureSealed.value());
        return Result<>::success({});
    } catch (const std::exception&) { return Result<>::failure(storageError()); }
}
void SqliteLocalStore::close() noexcept { impl_.reset(); }
}
