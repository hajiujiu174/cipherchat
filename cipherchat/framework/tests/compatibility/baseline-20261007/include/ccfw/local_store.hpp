#pragma once
#include "ccfw/result.hpp"
#include "ccfw/types.hpp"
#include <memory>

namespace ccfw {
// seal/unseal 不等于 Base64。Windows 实现使用当前用户 DPAPI；测试注入隔离实现。
// 错误必须返回失败；不可降级成明文存储。
class ISecretProtector {
public:
    virtual ~ISecretProtector() = default;
    virtual Result<cc::Bytes> seal(cc::BytesView) = 0;
    virtual Result<cc::Bytes> unseal(cc::BytesView) = 0;
};
class ILocalStore {
public:
    virtual ~ILocalStore() = default;
    virtual Result<> open(const std::filesystem::path&, const AccountScope&) = 0;
    // 初次打开返回 revision=0 且 identity=nullopt；损坏文件返回错误
    virtual Result<ClientSnapshot> load() = 0;
    // 一次 SQLite 事务提交完整快照。只接受 expectedRevision==库当前 revision，
    // next.revision 必须为 expectedRevision+1。失败时数据库完全不变。
    virtual Result<> commit(std::uint64_t expectedRevision, const ClientSnapshot& next) = 0;
    virtual void close() noexcept = 0;
};
class SqliteLocalStore final : public ILocalStore {
public:
    explicit SqliteLocalStore(ISecretProtector&);
    ~SqliteLocalStore() override;
    Result<> open(const std::filesystem::path&, const AccountScope&) override;
    Result<ClientSnapshot> load() override;
    Result<> commit(std::uint64_t, const ClientSnapshot&) override;
    void close() noexcept override;
private:
    struct Impl;
    ISecretProtector& protector_;
    std::unique_ptr<Impl> impl_;
};
class PlatformSecretProtector final : public ISecretProtector {
public:
    Result<cc::Bytes> seal(cc::BytesView) override;
    Result<cc::Bytes> unseal(cc::BytesView) override;
};
} // namespace ccfw

