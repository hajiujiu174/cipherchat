#pragma once
#include "ccfw/result.hpp"
#include "ccfw/types.hpp"
#include <memory>

namespace ccfw {
// DPAPI is retained only for importing old protected snapshots. New chat snapshots
// contain ordinary records, with no identity keys, ratchets or file encryption keys.
class ISecretProtector {
public:
    // 销毁local_store 模块时释放它持有的资源。
    virtual ~ISecretProtector() = default;
    // 保护local_store 模块。
    virtual Result<cc::Bytes> seal(cc::BytesView) = 0;
    // 解除保护local_store 模块。
    virtual Result<cc::Bytes> unseal(cc::BytesView) = 0;
};
class ILocalStore {
public:
    // 销毁local_store 模块时释放它持有的资源。
    virtual ~ILocalStore() = default;
    // 打开local_store 模块。
    virtual Result<> open(const std::filesystem::path&, const AccountScope&) = 0;
    // 初次打开返回 revision=0 且 identity=nullopt；损坏文件返回错误
    virtual Result<ClientSnapshot> load() = 0;
    // 一次 SQLite 事务提交完整快照。只接受 expectedRevision==库当前 revision，
    // next.revision 必须为 expectedRevision+1。失败时数据库完全不变。
    virtual Result<> commit(std::uint64_t expectedRevision, const ClientSnapshot& next) = 0;
    // 关闭local_store 模块。
    virtual void close() noexcept = 0;
};
class SqliteLocalStore final : public ILocalStore {
public:
    // 构造客户端 SQLite 存储，初始化后续操作需要的状态或依赖。
    explicit SqliteLocalStore(ISecretProtector&);
    // 销毁客户端 SQLite 存储时释放它持有的资源。
    ~SqliteLocalStore() override;
    // 打开指定账号范围的本机数据库，初始化必要表结构及兼容状态。
    Result<> open(const std::filesystem::path&, const AccountScope&) override;
    // 读取当前账号的本机快照和扩展数据，并按已有版本规则解码。
    Result<ClientSnapshot> load() override;
    // 核对账号范围及预期修订号，再用同一 SQLite 事务写入主快照和扩展功能状态。
    Result<> commit(std::uint64_t, const ClientSnapshot&) override;
    // 关闭客户端 SQLite 存储。
    void close() noexcept override;
private:
    struct Impl;
    ISecretProtector& protector_;
    std::unique_ptr<Impl> impl_;
};
class PlatformSecretProtector final : public ISecretProtector {
public:
    // 保护local_store 模块。
    Result<cc::Bytes> seal(cc::BytesView) override;
    // 解除保护local_store 模块。
    Result<cc::Bytes> unseal(cc::BytesView) override;
};
} // namespace ccfw

