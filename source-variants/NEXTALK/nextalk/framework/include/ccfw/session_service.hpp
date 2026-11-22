#pragma once
#include "ccfw/local_store.hpp"

namespace ccfw {
class ISessionService {
public:
    virtual ~ISessionService() = default;
    // Legacy adapter entry point; ordinary chat returns NotImplemented.
    virtual Result<> establish(const std::string& peer,
                               const cc::ratchet::X3dhPeerBundle&) = 0;
    // Historical method name: atomically persist ordinary JSON + history + outbox.
    virtual Result<WireEnvelope> encryptAndCommit(const PlainEnvelope&) = 0;
    // Historical method name: validate route, deduplicate and persist ordinary messages.
    virtual Result<PlainEnvelope> decryptAndCommit(const WireEnvelope&) = 0;
};
class SessionService final : public ISessionService {
public:
    explicit SessionService(ILocalStore& store) : store_(store) {}
    Result<> establish(const std::string&, const cc::ratchet::X3dhPeerBundle&) override;
    Result<WireEnvelope> encryptAndCommit(const PlainEnvelope&) override;
    Result<PlainEnvelope> decryptAndCommit(const WireEnvelope&) override;
    // Legacy adapter entry point; ordinary chat returns NotImplemented.
    Result<> acceptPeerIdentity(const PeerIdentity&);
private:
    ILocalStore& store_;
};
} // namespace ccfw
