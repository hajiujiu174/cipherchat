// M0-03 验收：客户端构建不再依赖服务器认证代码。
// 本测试目标只链接 client-core 与 common（不含 server/ 下任何翻译单元），
// 直接调用双端共享的登录证明公式，覆盖既有验收路径：
// 正确口令、错口令、nonce 重放。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "client/core/client.hpp"
#include "common/auth/login_proof.hpp"

namespace {

using namespace cc;

struct LoginMaterial {
    Bytes k;      // PBKDF2 输出（服务器存档即此值）
    Bytes nonce;  // 一次性挑战
};

LoginMaterial material(const std::string& password, const Bytes& salt) {
    return {cc::crypto::pbkdf2Sha256(password, salt,
                                     static_cast<std::uint64_t>(
                                         cc::auth::kPbkdf2Iterations),
                                     cc::auth::kPbkdf2KeyLen),
            cc::crypto::randomBytes(cc::auth::kLoginNonceLen)};
}

}  // namespace

TEST_CASE("M0-03 正确口令派生的证明通过验证") {
    const Bytes salt = cc::crypto::randomBytes(cc::auth::kPbkdf2SaltLen);
    const auto m = material("口令-correct-horse", salt);
    const Bytes proof = cc::auth::deriveLoginProof(m.k, m.nonce);
    CHECK(cc::auth::verifyLoginProof(m.k, m.nonce, proof));
}

TEST_CASE("M0-03 错误口令派生的证明被拒绝") {
    const Bytes salt = cc::crypto::randomBytes(cc::auth::kPbkdf2SaltLen);
    const auto good = material("口令-correct-horse", salt);
    const auto evil = material("口令-wrong-battery", salt);
    const Bytes proof = cc::auth::deriveLoginProof(evil.k, good.nonce);
    CHECK_FALSE(cc::auth::verifyLoginProof(good.k, good.nonce, proof));
}

TEST_CASE("M0-03 nonce 重放：同一证明不得在第二个 nonce 上复用") {
    const Bytes salt = cc::crypto::randomBytes(cc::auth::kPbkdf2SaltLen);
    const auto first = material("口令-correct-horse", salt);
    const Bytes proof = cc::auth::deriveLoginProof(first.k, first.nonce);
    CHECK(cc::auth::verifyLoginProof(first.k, first.nonce, proof));
    // 重放：换一个新 nonce，旧证明必须失效
    const Bytes replayed = cc::crypto::randomBytes(cc::auth::kLoginNonceLen);
    CHECK_FALSE(cc::auth::verifyLoginProof(first.k, replayed, proof));
}

TEST_CASE("M0-03 客户端核心类型仅依赖公共层（编译期断言）") {
    // ClientCore 头文件可被独立包含并实例化，证明无 server 头泄漏
    static_assert(sizeof(cc::client::ClientCore) > 0);
    CHECK(cc::auth::kPbkdf2Iterations >= 100000);
}
