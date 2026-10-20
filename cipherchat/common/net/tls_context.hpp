#pragma once
// TLS 1.3 上下文工厂（服务端 / 客户端共用）。
//
// 服务端证书：
//   - 文件模式：首次启动自动生成自签名 EC 证书（含 SAN）并落盘；
//   - 内存模式（测试）：现场生成临时 CA 并签发服务端证书，返回 CA PEM
//     作为客户端信任锚；validDays 可为负以构造过期证书（测试用）。
// 客户端：
//   - 正式路径：证书链验证 + 主机名验证（SSL_set1_host），不安装系统根证书，
//     信任锚由调用方显式提供；
//   - makeClientContextInsecure() 仅保留给 v1 旧版回归路径，正式路径禁止使用。
//
// 注：TLS 在此仅保护传输层；端到端保密性由 Double Ratchet 保证。

#include <cstddef>
#include <memory>
#include <string>

#include <openssl/ssl.h>

namespace cc::net {

struct SslCtxDeleter {
    void operator()(SSL_CTX* p) const { SSL_CTX_free(p); }
};
using SslCtx = std::unique_ptr<SSL_CTX, SslCtxDeleter>;

// 传输层资源限额（framework/docs/contracts.md §2），双端一致。
inline constexpr std::size_t kOutboxMaxFrames = 256;
inline constexpr std::size_t kOutboxMaxBytes = 16 * 1024 * 1024;
// poll 空转间隔：期间到达的出站帧至多延迟这么久发出
inline constexpr int kPollTimeoutMs = 100;

// 公共参数：仅 TLS 1.3、强加密套件
void configureCommon(SslCtx& ctx);

// 服务端上下文（文件模式）。证书不存在则自动生成自签名证书（含 SAN）。
SslCtx makeServerContext(const std::string& certPath,
                         const std::string& keyPath);

// 服务端上下文（内存模式，测试用）：现场生成临时 CA 并签发服务端证书。
// caValidDays 为服务端证书有效期天数；负值构造已过期证书。
struct MemoryServerTls {
    SslCtx server;
    std::string caCertPem;  // 客户端信任锚
};
MemoryServerTls makeMemoryServerTls(int caValidDays = 3650);

// 客户端正式路径：caPem 为信任锚证书 PEM，serverName 为预期主机名
//（同时用于 SNI 与证书主机名校验）。验证失败即握手失败。
SslCtx makeClientContextFromPem(const std::string& caPem,
                                const std::string& serverName);

// 客户端正式路径（文件信任锚）。
SslCtx makeClientContext(const std::string& caCertPath,
                         const std::string& serverName);

// 仅 v1 旧版回归路径：跳过证书校验。正式业务禁止调用。
SslCtx makeClientContextInsecure();

}  // namespace cc::net
