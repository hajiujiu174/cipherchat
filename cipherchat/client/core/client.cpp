#include "client/core/client.hpp"

#include "common/auth/login_proof.hpp"  // 双端共享的登录证明公式

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace cc::client {

using cc::proto::bodyBool;
using cc::proto::bodyInt;
using cc::proto::bodyStr;
using cc::proto::MsgType;

namespace {

// fileId 字符串 → 原始字节（作为 AAD 的一部分）
Bytes fileIdBytes(const std::string& fileId) {
    Bytes out;
    cc::base64Decode(fileId, out);
    return out;
}

}  // namespace

ClientCore::ClientCore() = default;

ClientCore::~ClientCore() {
    stopResendLoop();
    disconnect();
}

void ClientCore::connect(const std::string& host, std::uint16_t port) {
    net_.start(
        host, port,
        [this](const cc::proto::Message& msg) { handleMessage(msg); },
        [] {});
}

void ClientCore::disconnect() { net_.stop(); }

bool ClientCore::connected() const { return net_.connected(); }

std::string ClientCore::localUsername() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return identity_ ? identity_->username : std::string();
}

// ---- 注册 ----

void ClientCore::registerAccount(const std::string& username,
                                 const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = std::make_unique<Identity>();
    id->username = username;
    id->password = password;
    id->ik = cc::crypto::generateX25519();
    id->sign = cc::crypto::generateEd25519();
    id->spk = cc::crypto::generateX25519();
    id->spkSig = cc::crypto::ed25519Sign(id->sign.priv, id->spk.pub);

    const bool sent = net_.send(cc::proto::makeRegister(
        username, password, cc::base64Encode(id->sign.pub),
        cc::base64Encode(id->ik.pub), cc::base64Encode(id->spk.pub),
        cc::base64Encode(id->spkSig)));
    if (sent) identity_ = std::move(id);
}

// ---- 登录 ----

void ClientCore::login(const std::string& username,
                       const std::string& password) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 身份在注册（或将来从本地存储加载）后已具备；登录只补充口令
        if (identity_ && identity_->username == username) {
            identity_->password = password;
        }
    }
    net_.send(cc::proto::makeLoginRequest(username));
}

void ClientCore::handleLoginChallenge(const cc::proto::Message& msg) {
    Bytes nonce, salt;
    if (!cc::base64Decode(bodyStr(msg.body, "nonce"), nonce) ||
        !cc::base64Decode(bodyStr(msg.body, "salt"), salt)) {
        return;
    }
    const std::int64_t iters =
        bodyInt(msg.body, "iterations", cc::auth::kPbkdf2Iterations);
    std::string username, password;
    Bytes signPriv;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!identity_) return;
        username = identity_->username;
        password = identity_->password;
        signPriv = identity_->sign.priv;
    }
    const Bytes k =
        cc::crypto::pbkdf2Sha256(password, salt,
                                 static_cast<std::uint64_t>(iters),
                                 cc::auth::kPbkdf2KeyLen);
    const Bytes proof = cc::auth::deriveLoginProof(k, nonce);
    const Bytes sig = cc::crypto::ed25519Sign(signPriv, nonce);
    net_.send(cc::proto::makeLoginResponse(
        username, cc::base64Encode(proof), cc::base64Encode(nonce),
        cc::base64Encode(sig)));
}

// ---- 其余操作 ----

void ClientCore::uploadPrekeys(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!identity_ || count <= 0) return;
    std::vector<cc::proto::OneTimePrekey> pubs;
    for (int i = 0; i < count; ++i) {
        const auto kp = cc::crypto::generateX25519();
        const std::int64_t id = identity_->nextOtpId++;
        identity_->opkPrivs[id] = kp.priv;
        pubs.push_back({id, cc::base64Encode(kp.pub)});
    }
    net_.send(cc::proto::makeUploadPrekeys(pubs));
}

void ClientCore::fetchBundle(const std::string& username) {
    net_.send(cc::proto::makeFetchBundle(username));
}

void ClientCore::listUsers() { net_.send(cc::proto::makeListUsers()); }

void ClientCore::createGroup(const std::string& name,
                             const std::vector<std::string>& members) {
    net_.send(cc::proto::makeCreateGroup(name, members));
}

void ClientCore::sendTyping(const std::string& to) {
    net_.send(cc::proto::makeTypingTo(to));
}

// ---- 端到端文本 ----

Bytes ClientCore::buildAd(BytesView aIk, BytesView bIk) {
    // 字典序拼接，双端计算结果一致
    if (aIk < bIk) return Bytes(aIk) + Bytes(bIk);
    return Bytes(bIk) + Bytes(aIk);
}

bool ClientCore::sendEnvelope(const std::string& to, const std::string& kind,
                              const ratchet::Envelope& env,
                              std::int64_t groupId) {
    const std::string payload = cc::base64Encode(env.toJson().dump());
    return net_.send(cc::proto::makeSendMsg(
        "c-" + cc::base64Encode(cc::crypto::randomBytes(8)), to, kind, payload,
        groupId));
}

void ClientCore::enqueueOut(const std::string& to, const std::string& kind,
                            const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(to);
        if (it != sessions_.end()) {
            const ratchet::Envelope env =
                it->second.ratchet.encrypt(text, it->second.ad);
            sendEnvelope(to, kind, env);
            return;
        }
        pending_[to].push_back({kind, text});
    }
    fetchBundle(to);
}

void ClientCore::sendPlaintextEnvelope(const std::string& to,
                                       const std::string& kind,
                                       const std::string& plaintext,
                                       std::int64_t groupId) {
    const std::string payload = cc::base64Encode(plaintext);
    net_.send(cc::proto::makeSendMsg(
        "c-" + cc::base64Encode(cc::crypto::randomBytes(8)), to, kind, payload,
        groupId));
}

void ClientCore::sendChat(const std::string& to, const std::string& plaintext) {
    enqueueOut(to, "chat", plaintext);
}

// ---- 端到端文件 ----

bool ClientCore::sendFile(const std::string& to, const std::string& localPath) {
    Bytes data;
    if (!files::readFile(localPath, data)) return false;
    const std::size_t slash = localPath.find_last_of("/\\");
    const std::string name =
        slash == std::string::npos ? localPath : localPath.substr(slash + 1);

    const std::string fileId = cc::base64Encode(cc::crypto::randomBytes(
        files::kFileIdLen));
    const Bytes fileKey = cc::crypto::randomBytes(32);
    const std::uint32_t chunkCount =
        data.empty() ? 1
                     : static_cast<std::uint32_t>((data.size() +
                                                   files::kChunkSize - 1) /
                                                  files::kChunkSize);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!identity_) return false;
        FileSend st;
        st.to = to;
        st.fileId = fileId;
        st.name = name;
        st.chunkCount = chunkCount;
        st.acked.assign(chunkCount, false);
        // 预封全部块（块间 MAC 链在封块时定型）
        Bytes prevTag;
        for (std::uint32_t seq = 0; seq < chunkCount; ++seq) {
            const std::size_t off = static_cast<std::size_t>(seq) *
                                    files::kChunkSize;
            const BytesView plain(
                data.data() + off,
                std::min(files::kChunkSize, data.size() - off));
            const files::ChunkSeal seal =
                files::sealChunk(fileKey, fileIdBytes(fileId), seq, prevTag,
                                 plain);
            prevTag = seal.tag;
            st.sealed.push_back(seal);
        }
        fileSends_[fileId] = std::move(st);
        if (!resending_.exchange(true)) {
            resendThread_ = std::thread([this] { resendLoop(); });
        }
    }

    // file_meta 走棘轮会话端到端下发文件密钥
    nlohmann::json meta;
    meta["file_id"] = fileId;
    meta["name"] = name;
    meta["size"] = data.size();
    meta["chunks"] = chunkCount;
    meta["file_key"] = cc::base64Encode(fileKey);
    enqueueOut(to, "file_meta", meta.dump());
    return true;
}

void ClientCore::resendLoop() {
    // 周期扫描未确认块并补发；TLS 保证按序到达，ACK 缺失即重发
    int idlePasses = 0;
    while (resending_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        struct Job {
            std::string to;
            std::string fileId;
            std::uint32_t seq;
            std::string json;
        };
        std::vector<Job> jobs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [fileId, st] : fileSends_) {
                for (std::uint32_t seq = 0; seq < st.chunkCount; ++seq) {
                    if (st.acked[seq]) continue;
                    const files::ChunkSeal& seal = st.sealed[seq];
                    nlohmann::json j;
                    j["file_id"] = st.fileId;
                    j["seq"] = seq;
                    j["data"] = cc::base64Encode(seal.cipher);
                    j["tag"] = cc::base64Encode(seal.tag);
                    jobs.push_back({st.to, st.fileId, seq, j.dump()});
                    if (jobs.size() >= 64) break;  // 单轮限量，防风暴
                }
                if (jobs.size() >= 64) break;
            }
        }
        for (const Job& job : jobs) {
            sendPlaintextEnvelope(job.to, "file", job.json);
        }
        // 空闲约 6 秒且无进行中的发送 → 自动退出定时线程
        if (jobs.empty()) {
            if (++idlePasses > 20) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (fileSends_.empty()) {
                    resending_.store(false);
                    break;
                }
                idlePasses = 0;
            }
        } else {
            idlePasses = 0;
        }
    }
}

void ClientCore::stopResendLoop() {
    if (resending_.exchange(false)) {
        if (resendThread_.joinable() &&
            resendThread_.get_id() != std::this_thread::get_id()) {
            resendThread_.join();
        }
    }
}

void ClientCore::handleFileAck(const std::string& fileId, std::int64_t seq) {
    std::size_t received = 0, total = 0;
    bool done = false;
    std::string peer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fileSends_.find(fileId);
        if (it == fileSends_.end()) return;
        FileSend& st = it->second;
        if (seq >= 0 && seq < static_cast<std::int64_t>(st.chunkCount)) {
            st.acked[static_cast<std::uint32_t>(seq)] = true;
        }
        received = std::count(st.acked.begin(), st.acked.end(), true);
        total = st.chunkCount;
        peer = st.to;
        if (received == total) {
            done = true;
            fileSends_.erase(it);
        }
    }
    if (onFileProgress) {
        onFileProgress(fileId, peer, received, total, done, "");
    }
}

void ClientCore::handleFileChunk(const std::string& from,
                                 const std::string& payloadB64) {
    Bytes raw;
    if (!cc::base64Decode(payloadB64, raw)) return;
    nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    const std::string fileId = j.value("file_id", "");
    const std::uint32_t seq = static_cast<std::uint32_t>(j.value("seq", 0));
    Bytes data, tag;
    if (fileId.empty() || !cc::base64Decode(j.value("data", ""), data) ||
        !cc::base64Decode(j.value("tag", ""), tag)) {
        return;
    }

    std::string savePath;
    std::size_t received = 0, total = 0;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fileRecvs_.find(fileId);
        if (it == fileRecvs_.end() || it->second.done) return;  // meta 未到/已完成
        FileRecv& st = it->second;
        if (st.chunks.count(seq)) {
            // 重复块：补发一次 ACK 后丢弃
            cc::proto::Message ack = cc::proto::makeFileAck(fileId, seq);
            ack.body["to"] = from;
            net_.send(ack);
            return;
        }
        // 链式 MAC 要求按序接收
        if (seq != st.chunks.size()) return;
        auto plain = files::openChunk(st.fileKey, fileIdBytes(fileId), seq,
                                      st.prevTag, data, tag);
        if (!plain) {
            if (onError) onError("file_chunk", "文件块校验失败");
            return;
        }
        st.prevTag = tag;
        st.chunks[seq] = std::move(*plain);
        received = st.chunks.size();
        total = st.chunkCount;

        cc::proto::Message ack = cc::proto::makeFileAck(fileId, seq);
        ack.body["to"] = from;
        net_.send(ack);

        if (received == total) {
            st.done = true;
            done = true;
            Bytes full;
            full.reserve(st.size);
            for (std::uint32_t i = 0; i < total; ++i) {
                full.append(st.chunks[i]);
            }
            std::error_code ec;
            std::filesystem::create_directories(downloadsDir_, ec);
            const std::time_t now = std::time(nullptr);
            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "%lld",
                          static_cast<long long>(now));
            savePath = downloadsDir_ + "/" + stamp + "_" + st.name;
            files::writeFile(savePath, full);
            fileRecvs_.erase(it);
        }
    }
    if (onFileProgress) {
        onFileProgress(fileId, from, received, total, done, savePath);
    }
}

// ---- 接收 ----

void ClientCore::handleRecvMsg(const cc::proto::Message& msg) {
    const std::string from = bodyStr(msg.body, "from");
    const std::string kind = bodyStr(msg.body, "kind");
    const std::string payloadB64 = bodyStr(msg.body, "payload");
    const std::int64_t gid = bodyInt(msg.body, "group_id");

    if (kind == "file") {
        handleFileChunk(from, payloadB64);  // 块载荷不经过棘轮（fileKey 通道）
        return;
    }
    if (kind != "chat" && kind != "file_meta") return;

    const std::int64_t ts = bodyInt(msg.body, "ts");

    Bytes payloadBytes;
    if (!cc::base64Decode(payloadB64, payloadBytes)) return;
    nlohmann::json envJson = nlohmann::json::parse(payloadBytes, nullptr, false);
    if (envJson.is_discarded()) return;
    auto env = ratchet::Envelope::fromJson(envJson);
    if (!env) return;

    std::string plaintext;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!identity_) return;
        auto it = sessions_.find(from);
        if (it == sessions_.end()) {
            if (!env->header.hasPrekey()) {
                if (onError) onError("no_session", "缺少会话且非预密钥消息");
                return;
            }
            // 对端发起：重算 X3DH 建立会话
            Bytes opkPriv;
            if (env->header.opkId > 0) {
                auto opk = identity_->opkPrivs.find(env->header.opkId);
                if (opk != identity_->opkPrivs.end()) {
                    opkPriv = opk->second;
                    identity_->opkPrivs.erase(opk);  // 一次性：用后即焚
                }
            }
            const Bytes root = ratchet::x3dhRespond(
                identity_->ik.priv, identity_->spk.priv, opkPriv,
                env->header.ikPub, env->header.ekPub);
            Session s;
            s.ratchet = ratchet::DoubleRatchet::initAsResponder(
                root, {identity_->spk.priv, identity_->spk.pub});
            s.peerIkPub = env->header.ikPub;
            s.ad = buildAd(identity_->ik.pub, env->header.ikPub);
            sessions_[from] = std::move(s);
            it = sessions_.find(from);
        }
        cc::Bytes out;
        const auto st =
            it->second.ratchet.decrypt(env->header, env->ciphertext,
                                       it->second.ad, out);
        if (st != ratchet::DoubleRatchet::DecryptStatus::Ok) {
            if (onError) onError("decrypt_failed", "消息解密失败（可能被篡改）");
            return;
        }
        plaintext.assign(out.begin(), out.end());
    }

    if (kind == "chat") {
        if (onMessage) onMessage(from, plaintext, gid);
        return;
    }
    // file_meta：登记接收状态
    nlohmann::json meta = nlohmann::json::parse(plaintext, nullptr, false);
    if (meta.is_discarded() || !meta.is_object()) return;
    const std::string fileId = meta.value("file_id", "");
    Bytes fileKey;
    if (fileId.empty() || !cc::base64Decode(meta.value("file_key", ""), fileKey)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        FileRecv st;
        st.peer = from;
        st.name = meta.value("name", "file");
        st.size = static_cast<std::uint64_t>(meta.value("size", 0LL));
        st.chunkCount = static_cast<std::uint32_t>(meta.value("chunks", 0));
        st.fileKey = std::move(fileKey);
        fileRecvs_[fileId] = std::move(st);
    }
    if (onFileProgress) {
        onFileProgress(fileId, from, 0,
                       static_cast<std::size_t>(meta.value("chunks", 0)), false,
                       "");
    }
}

void ClientCore::startSessionAndSend(const std::string& to, const OutMsg& out,
                                     const ratchet::X3dhPeerBundle& peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!identity_) return;
    if (sessions_.count(to)) {
        // 会话已存在（并发到达）：直接按现有会话发送
        auto& s = sessions_[to];
        const ratchet::Envelope env = s.ratchet.encrypt(out.text, s.ad);
        sendEnvelope(to, out.kind, env);
        return;
    }
    const Bytes myIk = identity_->ik.pub;
    const auto init = ratchet::x3dhInitiate(identity_->ik.priv, myIk, peer);
    Session s;
    s.ratchet = ratchet::DoubleRatchet::initAsInitiator(init.rootKey,
                                                        peer.spkPub);
    s.peerIkPub = peer.ikPub;
    s.ad = buildAd(myIk, peer.ikPub);

    const ratchet::Envelope env = s.ratchet.encrypt(out.text, s.ad);
    // 首条消息携带 X3DH 材料
    ratchet::Envelope prekeyEnv = env;
    prekeyEnv.header.ikPub = init.ikPub;
    prekeyEnv.header.ekPub = init.ekPub;
    prekeyEnv.header.opkId = init.opkId;

    sessions_[to] = std::move(s);
    sendEnvelope(to, out.kind, prekeyEnv);
}

// ---- 分发 ----

void ClientCore::handleMessage(const cc::proto::Message& msg) {
    switch (msg.type) {
        case MsgType::RegisterResult:
            if (onRegisterResult) {
                onRegisterResult(bodyBool(msg.body, "ok"),
                                 bodyStr(msg.body, "error"));
            }
            break;
        case MsgType::LoginChallenge:
            handleLoginChallenge(msg);
            break;
        case MsgType::LoginResult:
            if (onLoginResult) {
                onLoginResult(bodyBool(msg.body, "ok"),
                              bodyStr(msg.body, "error"));
            }
            break;
        case MsgType::Bundle: {
            const std::string target = bodyStr(msg.body, "username");
            auto bundleOpt = bundleFromMessage(msg);
            std::vector<OutMsg> queue;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = pending_.find(target);
                if (it != pending_.end()) {
                    queue = std::move(it->second);
                    pending_.erase(it);
                }
            }
            if (bundleOpt && !queue.empty()) {
                startSessionAndSend(target, queue.front(), *bundleOpt);
                for (std::size_t i = 1; i < queue.size(); ++i) {
                    enqueueOut(target, queue[i].kind, queue[i].text);
                }
            }
            break;
        }
        case MsgType::RecvMsg:
            handleRecvMsg(msg);
            break;
        case MsgType::MsgAck:
            if (onAck) {
                onAck(msg.msgId, bodyBool(msg.body, "delivered"));
            }
            break;
        case MsgType::FileAck:
            handleFileAck(msg.msgId, bodyInt(msg.body, "seq"));
            break;
        case MsgType::Presence:
            if (onPresence) {
                onPresence(bodyStr(msg.body, "username"),
                           bodyBool(msg.body, "online"));
            }
            break;
        case MsgType::Typing:
            if (onTyping) onTyping(bodyStr(msg.body, "from"));
            break;
        case MsgType::UserList: {
            if (!onUserList) break;
            std::vector<std::pair<std::string, bool>> users;
            if (auto it = msg.body.find("users");
                it != msg.body.end() && it->is_array()) {
                for (const auto& u : *it) {
                    users.emplace_back(u.value("username", ""),
                                       u.value("online", false));
                }
            }
            onUserList(users);
            break;
        }
        case MsgType::GroupResult:
            if (onGroupResult) {
                std::vector<std::string> members;
                if (auto it = msg.body.find("members");
                    it != msg.body.end() && it->is_array()) {
                    for (const auto& m : *it) {
                        if (m.is_string()) {
                            members.push_back(m.get<std::string>());
                        }
                    }
                }
                onGroupResult(bodyBool(msg.body, "ok"),
                              bodyStr(msg.body, "error"),
                              bodyInt(msg.body, "group_id"),
                              bodyStr(msg.body, "name"), members);
            }
            break;
        case MsgType::GroupUpdate:
            if (onGroupUpdate) {
                std::vector<std::string> members;
                if (auto it = msg.body.find("members");
                    it != msg.body.end() && it->is_array()) {
                    for (const auto& m : *it) {
                        if (m.is_string()) {
                            members.push_back(m.get<std::string>());
                        }
                    }
                }
                onGroupUpdate(bodyInt(msg.body, "group_id"),
                              bodyStr(msg.body, "name"), members);
            }
            break;
        case MsgType::Error:
            if (onError) {
                onError(bodyStr(msg.body, "code"), bodyStr(msg.body, "text"));
            }
            break;
        default:
            break;
    }
}

std::optional<ratchet::X3dhPeerBundle> ClientCore::bundleFromMessage(
    const cc::proto::Message& msg) const {
    ratchet::X3dhPeerBundle b;
    // 服务器回传的均为 Base64 文本，此处统一还原为原始字节
    const auto decode = [&msg](const char* key, Bytes& out) {
        return cc::base64Decode(bodyStr(msg.body, key), out);
    };
    if (!decode("sign_pub", b.signPub) || !decode("dh_pub", b.ikPub) ||
        !decode("signed_prekey", b.spkPub) || !decode("spk_sig", b.spkSig)) {
        return std::nullopt;
    }
    const auto otp = msg.body.find("otp");
    if (otp != msg.body.end() && otp->is_object()) {
        b.opkId = otp->value("id", static_cast<std::int64_t>(0));
        cc::Bytes opkPub;
        if (!cc::base64Decode(otp->value("pub", ""), opkPub)) {
            return std::nullopt;
        }
        b.opkPub = std::move(opkPub);
    }
    if (b.ikPub.empty() || b.spkPub.empty()) return std::nullopt;
    return b;
}

bool ClientCore::hasSessionWith(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.count(username) != 0;
}

}  // namespace cc::client
