#pragma once
#include "ccfw/local_store.hpp"

namespace ccfw {
class ISessionService {
public:
    // 销毁session_service 模块时释放它持有的资源。
    virtual ~ISessionService() = default;
    // Legacy adapter entry point; ordinary chat returns NotImplemented.
    // Historical method name: atomically persist ordinary JSON + history + outbox.
    // 返回session_service 模块使用的encryptAnd提交。
    virtual Result<WireEnvelope> encryptAndCommit(const PlainEnvelope&) = 0;
    // Historical method name: validate route, deduplicate and persist ordinary messages.
    // 解密And提交。
    virtual Result<PlainEnvelope> decryptAndCommit(const WireEnvelope&) = 0;
};
class SessionService final : public ISessionService {
public:
    // 构造普通消息信封服务，初始化后续操作需要的状态或依赖。
    explicit SessionService(ILocalStore& store) : store_(store) {}
    // 按 ordinary-v1 编码普通消息信封，用同一事务保存待发送记录和文本历史。
    Result<WireEnvelope> encryptAndCommit(const PlainEnvelope&) override;
    // 解析普通消息信封，核对内外层路由字段及重复记录，再保存收到的内容。
    Result<PlainEnvelope> decryptAndCommit(const WireEnvelope&) override;
    // Legacy adapter entry point; ordinary chat returns NotImplemented.
private:
    ILocalStore& store_;
};
} // namespace ccfw
