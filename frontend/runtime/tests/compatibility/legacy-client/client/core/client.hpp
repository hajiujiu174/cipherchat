#pragma once
// 历史 v1 客户端，不是当前 NEXTALK 桌面的业务运行层。
// 当前服务端拒绝其旧注册/预共享密钥流程；正式运行路径为 framework 的 BusinessWorker。
// 客户端核心（无 GUI）：注册 / 挑战-响应登录 / 公钥束 / X3DH+双棘轮会话 /
// 端到端密文收发 / 离线消息补收 / presence / typing / 群组。
//
// 线程模型：NetClient 接收线程触发回调；对外回调均以 std::mutex 串行化，
// GUI 层只需把回调转发到事件循环即可。回调内可以直接调用本类方法（已加锁）。
//
// 端到端信封：send_msg.payload = Base64( JSON(Envelope) )，服务器不可读。

#include "client/core/files.hpp"
#include "client/core/net_client.hpp"
#include "common/crypto/crypto.hpp"
#include "common/ratchet/double_ratchet.hpp"
#include "common/ratchet/x3dh.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cc::client {

class ClientCore {
public:
    // ---- 对上层的事件回调 ----
    std::function<void(bool ok, const std::string& error)> onRegisterResult;
    std::function<void(bool ok, const std::string& error)> onLoginResult;
    std::function<void(const std::string& from, const std::string& kind,
                       const std::string& cipherPayload, std::int64_t ts,
                       std::int64_t groupId)>
        onRecvEnvelope;  // 原始信封（未解密），解密成功另触发 onMessage
    std::function<void(const std::string& from, const std::string& text,
                       std::int64_t groupId)>
        onMessage;  // 解密成功的明文（kind=chat）
    std::function<void(const std::string& msgId, bool delivered)> onAck;
    std::function<void(bool ok, const std::string& error, std::int64_t groupId,
                       const std::string& name,
                       const std::vector<std::string>& members)> onGroupResult;
    std::function<void(const std::string& user, bool online)> onPresence;
    std::function<void(const std::string& from)> onTyping;
    std::function<void(const std::vector<std::pair<std::string, bool>>&)> onUserList;
    std::function<void(std::int64_t groupId, const std::string& name,
                       const std::vector<std::string>& members)>
        onGroupUpdate;
    std::function<void(const std::string& code, const std::string& text)> onError;
    // 文件进度（收发双方均触发）：done=true 时 path 为最终文件路径（接收方）
    std::function<void(const std::string& fileId, const std::string& peer,
                       std::size_t received, std::size_t total, bool done,
                       const std::string& path)> onFileProgress;

    // 构造测试，初始化后续操作需要的状态或依赖。
    ClientCore();
    // 销毁测试时释放它持有的资源。
    ~ClientCore();

    // ---- 连接 ----
    void connect(const std::string& host, std::uint16_t port);
    // 断开测试。
    void disconnect();
    // 返回测试的连接是否已建立。
    bool connected() const;

    // ---- 账号 ----
    // 注册：在本地生成全部身份密钥并提交公钥材料
    void registerAccount(const std::string& username,
                         const std::string& password);
    // 返回测试使用的登录。
    void login(const std::string& username, const std::string& password);
    // 补充一次性预共享密钥（私钥留本地，公钥交服务器）
    void uploadPrekeys(int count);
    // 获取交付包。
    void fetchBundle(const std::string& username);
    // 列出用户。
    void listUsers();
    // 创建群聊。
    void createGroup(const std::string& name,
                     const std::vector<std::string>& members);
    // 发送正在输入。
    void sendTyping(const std::string& to);

    // ---- 端到端消息 ----
    // 发送文本。无会话时自动 fetch_bundle → X3DH → 发送（异步完成）。
    void sendChat(const std::string& to, const std::string& plaintext);

    // ---- 端到端文件 ----
    // 发送本地文件：file_meta（经棘轮）+ 加密分块（逐块 file_ack 确认，超时重发）
    bool sendFile(const std::string& to, const std::string& localPath);
    // 设置测试使用的DownloadsDir。
    void setDownloadsDir(const std::string& dir) { downloadsDir_ = dir; }

    // ---- 状态查询（已加锁）----
    bool hasSessionWith(const std::string& username);
    // 返回测试使用的本机用户名。
    std::string localUsername() const;

private:
    // 本端身份（长期）
    struct Identity {
        std::string username;
        std::string password;             // 仅登录期间驻留
        crypto::X25519KeyPair ik;         // 身份 DH
        crypto::Ed25519KeyPair sign;      // 签名
        crypto::X25519KeyPair spk;        // 签名预共享密钥
        Bytes spkSig;
        std::int64_t nextOtpId = 1;
        std::map<std::int64_t, Bytes> opkPrivs;  // 一次性私钥（id → 私钥）
    };

    struct Session {
        ratchet::DoubleRatchet ratchet;
        Bytes peerIkPub;
        Bytes ad;  // 会话绑定材料（双方身份公钥，字典序拼接）
    };

    // 待发队列条目（无会话时暂存，束到达后按序发出）
    struct OutMsg {
        std::string kind;   // "chat" | "file_meta"
        std::string text;   // 明文 / file_meta JSON
    };

    // 发送侧文件状态
    struct FileSend {
        std::string to;
        std::string fileId;
        std::string name;
        Bytes fileKey;
        std::uint32_t chunkCount = 0;
        std::vector<files::ChunkSeal> sealed;  // 预封好的全部块（含链式标签）
        std::vector<bool> acked;
    };

    // 接收侧文件状态
    struct FileRecv {
        std::string peer;
        std::string name;
        std::uint64_t size = 0;
        std::uint32_t chunkCount = 0;
        Bytes fileKey;
        Bytes prevTag;                  // 链式校验：须按序接收
        std::map<std::uint32_t, Bytes> chunks;
        bool done = false;
    };

    // 处理消息。
    void handleMessage(const cc::proto::Message& msg);
    // 处理登录挑战。
    void handleLoginChallenge(const cc::proto::Message& msg);
    // 处理接收消息通知。
    void handleRecvMsg(const cc::proto::Message& msg);
    // 处理文件文件块。
    void handleFileChunk(const std::string& from, const std::string& payloadB64);
    // 处理文件块进度确认。
    void handleFileAck(const std::string& fileId, std::int64_t seq);
    // 加入队列Out。
    void enqueueOut(const std::string& to, const std::string& kind,
                    const std::string& text);
    // 启动会话AndSend。
    void startSessionAndSend(const std::string& to, const OutMsg& out,
                             const ratchet::X3dhPeerBundle& peer);
    // 发送Plaintext消息信封。
    void sendPlaintextEnvelope(const std::string& to, const std::string& kind,
                               const std::string& plaintext,
                               std::int64_t groupId = 0);
    // 返回测试使用的交付包From消息。
    std::optional<ratchet::X3dhPeerBundle> bundleFromMessage(
        const cc::proto::Message& msg) const;
    // 构建Ad。
    static Bytes buildAd(BytesView aIk, BytesView bIk);
    // 发送消息信封。
    bool sendEnvelope(const std::string& to, const std::string& kind,
                      const ratchet::Envelope& env, std::int64_t groupId = 0);
    // 返回测试使用的resend循环。
    void resendLoop();
    // 停止Resend循环。
    void stopResendLoop();

    NetClient net_;
    mutable std::mutex mutex_;
    std::unique_ptr<Identity> identity_;
    std::map<std::string, Session> sessions_;       // 对端用户名 → 会话
    std::map<std::string, std::vector<OutMsg>> pending_;  // 待束发送的消息
    std::map<std::string, FileSend> fileSends_;     // fileId → 发送状态
    std::map<std::string, FileRecv> fileRecvs_;     // fileId → 接收状态
    std::string downloadsDir_ = "downloads";

    // 重发定时线程（首次发文件时惰性启动）
    std::thread resendThread_;
    std::atomic<bool> resending_{false};
};

}  // namespace cc::client
