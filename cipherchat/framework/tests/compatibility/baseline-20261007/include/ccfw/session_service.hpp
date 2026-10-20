#pragma once
#include "ccfw/local_store.hpp"

namespace ccfw {
class ISessionService {
public:
    virtual ~ISessionService() = default;
    // 必须先校验已信任身份和束签名；首次未知身份返回 IdentityChanged 等待外部确认。
    virtual Result<> establish(const std::string& peer,
                               const cc::ratchet::X3dhPeerBundle&) = 0;
    // 在副本上加密；原子提交 ratchet + history + outbox，提交后才可发送。
    virtual Result<WireEnvelope> encryptAndCommit(const PlainEnvelope&) = 0;
    // 认证成功后一次提交 ratchet + 去重记录 + history + OPK 消耗。
    // 失败不得消耗 skipped key、OPK 或改变持久状态。
    virtual Result<PlainEnvelope> decryptAndCommit(const WireEnvelope&) = 0;
};
class SessionService final : public ISessionService {
public:
    explicit SessionService(ILocalStore& store) : store_(store) {}
    Result<> establish(const std::string&, const cc::ratchet::X3dhPeerBundle&) override;
    Result<WireEnvelope> encryptAndCommit(const PlainEnvelope&) override;
    Result<PlainEnvelope> decryptAndCommit(const WireEnvelope&) override;
    // 由显式确认身份的 UI/CLI 操作调用；不是自动接受服务器返回的公钥
    Result<> acceptPeerIdentity(const PeerIdentity&);
private:
    ILocalStore& store_;
};
} // namespace ccfw
