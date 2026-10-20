#pragma once
// Double Ratchet：参考 Signal 算法的课程实现；自定义线格式不与 Signal 互通。
//
// - 对称棘轮：每条消息由链密钥 HKDF 派生一次性消息密钥后立即前进，
//   已删除的旧密钥不能从当前链反推；乱序缓存中的密钥仍须视为秘密。
// - DH 棘轮：收发方向切换时生成新 X25519 密钥对与对端公钥重新协商根密钥，
//   恢复保密依赖攻击者未知的新 DH 随机量及旧秘密清除，不能承诺任意回复一次即安全。
// - 乱序容忍：收方为跳过的序号缓存消息密钥（最多 kMaxSkip 条），后到可补解。
//
// 线上信封 Envelope 序列化为 JSON 后整体 Base64 放入 send_msg.payload。

#include "common/base64.hpp"
#include "common/crypto/crypto.hpp"

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace cc::ratchet {

// 单条消息的棘轮头。发起会话的首条消息额外携带 X3DH 材料。
struct Header {
    cc::Bytes dhPub;        // 发送方当前棘轮 X25519 公钥
    std::uint32_t pn = 0;   // 上一条发送链的长度
    std::uint32_t n = 0;    // 本条在当前发送链中的序号
    cc::Bytes ikPub;        // （仅首条）发起方身份 X25519 公钥
    cc::Bytes ekPub;        // （仅首条）发起方临时公钥
    std::int64_t opkId = 0;  // （仅首条）消耗的一次性密钥 id，0=未用

    bool hasPrekey() const { return !ekPub.empty(); }

    nlohmann::json toJson() const;
    static Header fromJson(const nlohmann::json& j);
};

// 密文信封：头 + AES-256-GCM 密文（含标签）。
struct Envelope {
    Header header;
    cc::Bytes ciphertext;

    nlohmann::json toJson() const;
    // 反序列化失败（字段缺失/损坏）返回 nullopt
    static std::optional<Envelope> fromJson(const nlohmann::json& j);
};

// 单条链上允许跳过（缓存）的消息密钥上限，防止对端恶意大序号撑爆内存。
inline constexpr std::uint32_t kMaxSkip = 64;

class DoubleRatchet {
public:
    // 发起方（X3DH 之后）：以对端 SPK 公钥为初始远端棘轮公钥起步。
    static DoubleRatchet initAsInitiator(const cc::Bytes& rootKey,
                                         BytesView peerSpkPub);
    // 响应方（收到首条 prekey 消息时）：以自己的 SPK 密钥对为初始棘轮密钥对。
    static DoubleRatchet initAsResponder(const cc::Bytes& rootKey,
                                         const crypto::X25519KeyPair& spkPair);

    // 加密一条消息。ad 为会话级绑定材料（如双方身份公钥拼接），双端须一致。
    Envelope encrypt(BytesView plaintext, BytesView ad = {});

    enum class DecryptStatus {
        Ok,          // 成功
        Failed,      // 认证失败：密文/AAD 被篡改，或消息不属于本会话
        TooManySkip, // 跳过数超限（疑似对端作恶或长时离线），应重建会话
        BadState,    // 本地状态与消息不匹配（如响应方缺 CKr 却收到 n>0）
    };
    DecryptStatus decrypt(const Header& header, BytesView ciphertext,
                          BytesView ad, cc::Bytes& plaintextOut);

    // ---- 持久化（SQLite 会话表 / 会话恢复用）----
    nlohmann::json toJson() const;
    static DoubleRatchet fromJson(const nlohmann::json& j);

    // 调试/界面展示
    std::uint32_t sentCount() const { return ns_; }
    std::uint32_t recvCount() const { return nr_; }
    std::size_t skippedKeys() const { return skipped_.size(); }
    const cc::Bytes& remoteRatchetPub() const { return dhr_; }

private:
    void dhRatchet(const Header& header);          // 方向切换
    DecryptStatus skipMessageKeys(std::uint32_t until);  // 缓存跳过的密钥

    // 状态字段（命名对齐 Signal 规范）
    crypto::X25519KeyPair dhs_;  // 本端棘轮密钥对
    cc::Bytes dhr_;              // 对端棘轮公钥（响应方初始为空）
    cc::Bytes rk_;               // 根密钥
    cc::Bytes cks_;              // 发送链密钥（未建立时为空）
    cc::Bytes ckr_;              // 接收链密钥（未建立时为空）
    std::uint32_t ns_ = 0, nr_ = 0, pn_ = 0;
    // key = "对端棘轮公钥B64:序号" → 跳过时缓存的消息密钥
    std::map<std::string, cc::Bytes> skipped_;
};

}  // namespace cc::ratchet
