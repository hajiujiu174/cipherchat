#include "ccfw/account_service.hpp"
#include "common/auth/login_proof.hpp"
#include <openssl/crypto.h>
namespace ccfw {
namespace {
Result<> accountError(ErrorCode code, const char* message) { return Result<>::failure({code, "M3-02", message}); }
bool validCredentials(const std::string& user, const std::string& password) {
    return user.size() >= 2 && user.size() <= 32 && password.size() >= 6 && password.size() <= 1024;
}
}
AccountService::AccountService(ITransport& net, ILocalStore& store, Clock clock)
    : net_(net), store_(store), clock_(clock ? std::move(clock) : Clock{[] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }}) {}
AccountService::~AccountService() { clearPassword(); }
void AccountService::clearPassword() noexcept {
    if (!password_.empty()) OPENSSL_cleanse(password_.data(), password_.size());
    password_.clear();
}
void AccountService::beginWait(AccountPhase phase) { phase_ = phase; deadlineMs_ = clock_() + 10000; }
Result<> AccountService::checkTimeout() {
    if (deadlineMs_ && clock_() >= deadlineMs_) {
        clearPassword(); deadlineMs_ = 0; responseSent_ = false;
        // 关闭连接避免迟到响应被下一次认证请求误接收。
        net_.close(); phase_ = AccountPhase::Disconnected;
        return accountError(ErrorCode::Timeout, "服务器认证响应超时，请重新连接");
    }
    return Result<>::success({});
}
Result<> AccountService::negotiateProtocol() {
    if (phase_ != AccountPhase::TransportConnected) return accountError(ErrorCode::InvalidState, "请先建立安全连接");
    auto sent = net_.send(cc::proto::makeHello());
    if (!sent) return sent;
    beginWait(AccountPhase::Negotiating);
    return Result<>::success({});
}
Result<> AccountService::registerAccount(const std::string& user, const std::string& password) {
    if (phase_ != AccountPhase::Connected) return accountError(ErrorCode::InvalidState, "当前不可注册，请等待连接或当前请求完成");
    if (!validCredentials(user, password)) return accountError(ErrorCode::InvalidArgument, "用户名或口令长度非法");
    auto loaded = store_.load();
    if (!loaded) return Result<>::failure(loaded.error());
    auto next = loaded.value();
    if (next.account.username != user) return accountError(ErrorCode::InvalidArgument, "账号与本地存储不匹配");
    try {
        if (!next.identity) {
            IdentityState identity;
            identity.signing = cc::crypto::generateEd25519();
            identity.identityDh = cc::crypto::generateX25519();
            identity.signedPrekey = cc::crypto::generateX25519();
            identity.signedPrekeySignature = cc::crypto::ed25519Sign(identity.signing.priv, identity.signedPrekey.pub);
            next.identity = std::move(identity);
            const auto expected = next.revision++;
            const auto saved = store_.commit(expected, next);
            if (!saved) return saved;
        }
        const auto& identity = *next.identity;
        auto sent = net_.send(cc::proto::withV2(cc::proto::makeRegister(user, password,
            cc::base64Encode(identity.signing.pub), cc::base64Encode(identity.identityDh.pub),
            cc::base64Encode(identity.signedPrekey.pub), cc::base64Encode(identity.signedPrekeySignature))));
        if (!sent) return sent;
        beginWait(AccountPhase::Registering);
        return Result<>::success({});
    } catch (const std::exception&) { return accountError(ErrorCode::CryptoFailure, "无法生成本地身份"); }
}
Result<> AccountService::login(const std::string& user, const std::string& password) {
    if (phase_ != AccountPhase::Connected) return accountError(ErrorCode::InvalidState, "当前不可登录，请等待连接或当前请求完成");
    if (!validCredentials(user, password)) return accountError(ErrorCode::InvalidArgument, "用户名或口令长度非法");
    auto snapshot = store_.load();
    if (!snapshot) return Result<>::failure(snapshot.error());
    if (snapshot.value().account.username != user) return accountError(ErrorCode::InvalidArgument, "账号与本地存储不匹配");
    if (!snapshot.value().identity) return accountError(ErrorCode::NotFound, "本机缺少该账号的私钥，无法登录");
    clearPassword(); password_ = password; responseSent_ = false;
    auto sent = net_.send(cc::proto::withV2(cc::proto::makeLoginRequest(user)));
    if (!sent) { clearPassword(); return sent; }
    beginWait(AccountPhase::ChallengePending);
    return Result<>::success({});
}
Result<> AccountService::uploadPrekeys(std::size_t count) {
    if (phase_ != AccountPhase::Online) return accountError(ErrorCode::InvalidState, "请先登录");
    if (!count || count > 128) return accountError(ErrorCode::InvalidArgument, "一次性预共享密钥数量须为 1–128");
    auto snapshot = store_.load();
    if (!snapshot) return Result<>::failure(snapshot.error());
    auto next = snapshot.value();
    if (!next.identity || next.identity->oneTimePrivateKeys.size() + count > 4096 ||
        next.identity->nextPrekeyId > INT64_MAX - static_cast<std::int64_t>(count))
        return accountError(ErrorCode::InvalidState, "本地密钥数量已达上限");
    try {
        std::vector<cc::proto::OneTimePrekey> keys;
        for (std::size_t i = 0; i < count; ++i) {
            auto key = cc::crypto::generateX25519(); const auto id = next.identity->nextPrekeyId++;
            keys.push_back({id, cc::base64Encode(key.pub)});
            next.identity->oneTimePrivateKeys.emplace(id, std::move(key.priv));
        }
        const auto expected = next.revision++;
        auto saved = store_.commit(expected, next); if (!saved) return saved;
        return net_.send(cc::proto::withV2(cc::proto::makeUploadPrekeys(keys)));
    } catch (const std::exception&) { return accountError(ErrorCode::CryptoFailure, "生成预共享密钥失败"); }
}
Result<> AccountService::handle(const cc::proto::Message& message) {
    using namespace cc::proto;
    if (auto timeout = checkTimeout(); !timeout) return timeout;
    auto invalid = [this] {
        clearPassword(); deadlineMs_ = 0; responseSent_ = false;
        net_.close(); phase_ = AccountPhase::Disconnected;
        return accountError(ErrorCode::ProtocolError, "服务器认证响应格式或顺序非法");
    };
    if (!isV2(message)) return invalid();
    if (message.type == MsgType::HelloResult && phase_ == AccountPhase::Negotiating) {
        if (!bodyBool(message.body, "ok")) return invalid();
        phase_ = AccountPhase::Connected; deadlineMs_ = 0;
        return Result<>::success({});
    }
    if (message.type == MsgType::RegisterResult && phase_ == AccountPhase::Registering) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean()) return invalid();
        phase_ = AccountPhase::Connected; deadlineMs_ = 0;
        return bodyBool(message.body, "ok") ? Result<>::success({}) : accountError(ErrorCode::Conflict, "注册未成功，账号可能已存在");
    }
    if (message.type == MsgType::LoginChallenge && phase_ == AccountPhase::ChallengePending && !responseSent_) {
        cc::Bytes nonce, salt;
        const auto iterations = bodyInt(message.body, "iterations");
        if (!cc::base64Decode(bodyStr(message.body, "nonce"), nonce) || nonce.size() != 16 ||
            !cc::base64Decode(bodyStr(message.body, "salt"), salt) || salt.size() != 16 || iterations < 10000 || iterations > 1000000)
            return invalid();
        auto snapshot = store_.load();
        if (!snapshot || !snapshot.value().identity) { clearPassword(); return invalid(); }
        try {
            auto key = cc::crypto::pbkdf2Sha256(password_, salt, iterations, 32);
            clearPassword();
            const auto proof = cc::auth::deriveLoginProof(key, nonce);
            OPENSSL_cleanse(key.data(), key.size());
            const auto signature = cc::crypto::ed25519Sign(snapshot.value().identity->signing.priv, nonce);
            auto sent = net_.send(withV2(makeLoginResponse(snapshot.value().account.username,
                cc::base64Encode(proof), cc::base64Encode(nonce), cc::base64Encode(signature))));
            if (!sent) { connectionChanged(false); net_.close(); return sent; }
            responseSent_ = true;
            return Result<>::success({});
        } catch (const std::exception&) { return invalid(); }
    }
    if (message.type == MsgType::LoginResult && phase_ == AccountPhase::ChallengePending) {
        if (!message.body.contains("ok") || !message.body["ok"].is_boolean() || (bodyBool(message.body, "ok") && !responseSent_)) return invalid();
        clearPassword(); deadlineMs_ = 0; responseSent_ = false;
        const bool ok = bodyBool(message.body, "ok"); phase_ = ok ? AccountPhase::Online : AccountPhase::Connected;
        return ok ? Result<>::success({}) : accountError(ErrorCode::AuthenticationFailed, "登录失败，请检查口令和本地身份");
    }
    if (message.type == MsgType::UploadPrekeysResult && phase_ == AccountPhase::Online) {
        return bodyBool(message.body, "ok") ? Result<>::success({}) : accountError(ErrorCode::ProtocolError, "预共享密钥上传失败");
    }
    return invalid();
}
void AccountService::connectionChanged(bool connected) noexcept {
    clearPassword(); responseSent_ = false; deadlineMs_ = 0;
    phase_ = connected ? AccountPhase::TransportConnected : AccountPhase::Disconnected;
}
}
