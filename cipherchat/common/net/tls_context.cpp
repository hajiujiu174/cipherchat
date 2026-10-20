#include "common/net/tls_context.hpp"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace cc::net {

namespace {

using KeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

KeyPtr generateEcKey() {
    KeyPtr key(nullptr, EVP_PKEY_free);
    EVP_PKEY_CTX* raw = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!raw) throw std::runtime_error("EVP_PKEY_CTX_new_id 失败");
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        raw, EVP_PKEY_CTX_free);
    if (EVP_PKEY_keygen_init(ctx.get()) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(),
                                               NID_X9_62_prime256v1) != 1) {
        throw std::runtime_error("EC 参数设置失败");
    }
    EVP_PKEY* raw_key = nullptr;
    if (EVP_PKEY_generate(ctx.get(), &raw_key) != 1) {
        throw std::runtime_error("EC 密钥生成失败");
    }
    key.reset(raw_key);
    return key;
}

// 追加 X.509 v3 扩展
void addExtension(X509* cert, int nid, const std::string& value) {
    X509_EXTENSION* ext = X509V3_EXT_nconf_nid(
        nullptr, nullptr, nid, const_cast<char*>(value.c_str()));
    if (!ext) throw std::runtime_error("X509 扩展构造失败");
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> holder(
        ext, X509_EXTENSION_free);
    if (X509_add_ext(cert, ext, -1) != 1) {
        throw std::runtime_error("X509 扩展写入失败");
    }
}

// 用 issuerKey 签发证书；issuerCert == nullptr 时自签名
X509Ptr makeCert(EVP_PKEY* subjectKey, EVP_PKEY* issuerKey, X509* issuerCert,
                 int validDays, bool asCa) {
    X509Ptr cert(X509_new(), X509_free);
    if (!cert) throw std::runtime_error("X509_new 失败");
    X509_set_version(cert.get(), 2);  // v3：SAN/基本约束需要
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), -3600);  // 时钟偏差容忍
    X509_gmtime_adj(X509_getm_notAfter(cert.get()),
                    static_cast<long>(validDays) * 24 * 3600);
    X509_set_pubkey(cert.get(), subjectKey);
    X509_NAME* subject = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(
        subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(asCa ? "CipherChat Test CA"
                                                    : "CipherChat"),
        -1, -1, 0);
    if (issuerCert) {
        X509_set_issuer_name(cert.get(), X509_get_subject_name(issuerCert));
    } else {
        X509_set_issuer_name(cert.get(), subject);
    }
    if (asCa) {
        addExtension(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    } else {
        addExtension(cert.get(), NID_basic_constraints, "critical,CA:FALSE");
        // 主机名校验依赖 SAN（现代校验器已忽略 CN）
        addExtension(cert.get(), NID_subject_alt_name,
                     "DNS:localhost,IP:127.0.0.1");
    }
    if (X509_sign(cert.get(), issuerKey ? issuerKey : subjectKey,
                  EVP_sha256()) == 0) {
        throw std::runtime_error("证书签名失败");
    }
    return cert;
}

std::string certToPem(const X509& cert) {
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_X509(bio.get(), &cert) != 1) {
        throw std::runtime_error("证书 PEM 导出失败");
    }
    char* data = nullptr;
    const long len = BIO_get_mem_data(bio.get(), &data);
    return std::string(data, static_cast<std::size_t>(len));
}

std::string readPemFile(const std::string& path) {
    std::ifstream file(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())), std::ios::binary);
    if (!file) throw std::runtime_error("无法打开 PEM 文件: " + path);
    std::string data{std::istreambuf_iterator<char>(file), {}};
    if (file.bad() || data.empty()) throw std::runtime_error("无法读取 PEM 文件: " + path);
    return data;
}

void writePemFile(const std::string& path, const std::string& pem) {
    std::ofstream file(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())), std::ios::binary);
    if (!file || !file.write(pem.data(), static_cast<std::streamsize>(pem.size())) || !file.flush())
        throw std::runtime_error("无法写入 PEM 文件: " + path);
}

void loadCaIntoStore(SSL_CTX& ctx, const std::string& caPem) {
    BioPtr bio(BIO_new_mem_buf(caPem.data(), static_cast<int>(caPem.size())),
               BIO_free);
    if (!bio) throw std::runtime_error("CA 内存 BIO 失败");
    X509* ca = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
    if (!ca) throw std::runtime_error("CA 证书解析失败");
    X509Ptr holder(ca, X509_free);
    X509_STORE* store = SSL_CTX_get_cert_store(&ctx);
    if (!store || X509_STORE_add_cert(store, ca) != 1) {
        throw std::runtime_error("信任锚写入失败");
    }
}

}  // namespace

void configureCommon(SslCtx& ctx) {
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION);
}

SslCtx makeServerContext(const std::string& certPath,
                         const std::string& keyPath) {
    SslCtx ctx(SSL_CTX_new(TLS_server_method()));
    if (!ctx) throw std::runtime_error("SSL_CTX_new 失败");
    configureCommon(ctx);

    if (certPath.empty() || keyPath.empty()) {
        // 测试/内存模式：不落盘，每次现场生成（含 SAN，供主机名校验）
        KeyPtr key = generateEcKey();
        X509Ptr cert = makeCert(key.get(), nullptr, nullptr, 3650, false);
        if (SSL_CTX_use_certificate(ctx.get(), cert.get()) != 1 ||
            SSL_CTX_use_PrivateKey(ctx.get(), key.get()) != 1 ||
            SSL_CTX_check_private_key(ctx.get()) != 1) {
            throw std::runtime_error("装载内存证书失败");
        }
        return ctx;
    }
    // 证书不存在时现场生成（首启自举，含 SAN）
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(certPath.data()), certPath.size())))) {
        if (std::filesystem::exists(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(keyPath.data()), keyPath.size()))))
            throw std::runtime_error("证书缺失但私钥已存在，拒绝覆盖私钥");
        KeyPtr key = generateEcKey();
        X509Ptr cert = makeCert(key.get(), nullptr, nullptr, 3650, false);
        BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
        if (!bio || PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
            throw std::runtime_error("私钥 PEM 导出失败");
        char* data = nullptr;
        const auto len = BIO_get_mem_data(bio.get(), &data);
        writePemFile(keyPath, std::string(data, static_cast<std::size_t>(len)));
        writePemFile(certPath, certToPem(*cert));
    }
    // OpenSSL DLL and MinGW may use different CRTs. Never pass application FILE* across that boundary.
    const auto certPem = readPemFile(certPath), keyPem = readPemFile(keyPath);
    BioPtr certBio(BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size())), BIO_free);
    BioPtr keyBio(BIO_new_mem_buf(keyPem.data(), static_cast<int>(keyPem.size())), BIO_free);
    X509Ptr cert(certBio ? PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
    KeyPtr key(keyBio ? PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
    if (!cert || !key || SSL_CTX_use_certificate(ctx.get(), cert.get()) != 1 ||
        SSL_CTX_use_PrivateKey(ctx.get(), key.get()) != 1 ||
        SSL_CTX_check_private_key(ctx.get()) != 1) {
        throw std::runtime_error("加载服务器证书/私钥失败");
    }
    return ctx;
}

MemoryServerTls makeMemoryServerTls(int caValidDays) {
    MemoryServerTls out;
    // 临时 CA（信任锚）
    KeyPtr caKey = generateEcKey();
    X509Ptr caCert = makeCert(caKey.get(), nullptr, nullptr, 3650, true);
    out.caCertPem = certToPem(*caCert);

    // 由 CA 签发服务端证书（负天数构造过期证书供测试）
    KeyPtr srvKey = generateEcKey();
    X509Ptr srvCert =
        makeCert(srvKey.get(), caKey.get(), caCert.get(), caValidDays, false);

    out.server.reset(SSL_CTX_new(TLS_server_method()));
    if (!out.server) throw std::runtime_error("SSL_CTX_new 失败");
    configureCommon(out.server);
    if (SSL_CTX_use_certificate(out.server.get(), srvCert.get()) != 1 ||
        SSL_CTX_use_PrivateKey(out.server.get(), srvKey.get()) != 1 ||
        SSL_CTX_check_private_key(out.server.get()) != 1) {
        throw std::runtime_error("装载内存服务端证书失败");
    }
    return out;
}

SslCtx makeClientContextFromPem(const std::string& caPem,
                                const std::string& serverName) {
    if (caPem.empty() || serverName.empty()) {
        throw std::runtime_error("正式客户端上下文必须提供信任锚与主机名");
    }
    SslCtx ctx(SSL_CTX_new(TLS_client_method()));
    if (!ctx) throw std::runtime_error("SSL_CTX_new 失败");
    configureCommon(ctx);
    loadCaIntoStore(*ctx, caPem);
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_verify_depth(ctx.get(), 2);
    return ctx;
}

SslCtx makeClientContext(const std::string& caCertPath,
                         const std::string& serverName) {
    if (caCertPath.empty()) {
        throw std::runtime_error("正式客户端上下文必须提供 CA 文件");
    }
    return makeClientContextFromPem(readPemFile(caCertPath), serverName);
}

SslCtx makeClientContextInsecure() {
    SslCtx ctx(SSL_CTX_new(TLS_client_method()));
    if (!ctx) throw std::runtime_error("SSL_CTX_new 失败");
    configureCommon(ctx);
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    return ctx;
}

}  // namespace cc::net
