#pragma once
// 兼容转发层：登录证明公式已迁移到 common/auth（M0-03）。
// 服务器侧代码经由本头继续使用 cc::server:: 名称；客户端不得包含本文件。

#include "common/auth/login_proof.hpp"

namespace cc::server {
using cc::auth::deriveLoginProof;
using cc::auth::kLoginNonceLen;
using cc::auth::kPbkdf2Iterations;
using cc::auth::kPbkdf2KeyLen;
using cc::auth::kPbkdf2SaltLen;
using cc::auth::verifyLoginProof;
}  // namespace cc::server
