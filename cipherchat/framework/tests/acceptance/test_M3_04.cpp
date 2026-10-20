#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ccfw/local_store.hpp"

using namespace ccfw;
TEST_CASE("M3-04 current-user DPAPI roundtrips binary and empty data without plaintext storage") {
    PlatformSecretProtector protector;
#ifdef _WIN32
    cc::Bytes secret;
    SUBCASE("empty") {}
    SUBCASE("all byte values") { for (int i = 0; i < 256; ++i) secret.push_back(static_cast<char>(i)); }
    SUBCASE("large snapshot") { secret = cc::Bytes(1024 * 1024, 's'); }
    auto sealed = protector.seal(secret);
    REQUIRE(sealed.ok());
    const bool differs = sealed.value() != secret;
    CHECK(differs);
    PlatformSecretProtector secondInstance;
    auto unsealed = secondInstance.unseal(sealed.value());
    REQUIRE(unsealed.ok());
    const bool equal = unsealed.value() == secret;
    CHECK(equal);
#else
    CHECK_FALSE(protector.seal("test").ok());
#endif
}
TEST_CASE("M3-04 truncated, altered and unsupported formats fail closed") {
    PlatformSecretProtector protector;
#ifdef _WIN32
    auto encrypted = protector.seal("local-secret-test-value");
    REQUIRE(encrypted.ok());
    auto invalid = encrypted.value();
    SUBCASE("truncated header") { invalid.resize(7); }
    SUBCASE("truncated payload") { invalid.pop_back(); }
    SUBCASE("altered payload") { invalid.back() ^= 0x40; }
    SUBCASE("version") { invalid[7] = '2'; }
    SUBCASE("trailing data") { invalid += 'x'; }
    SUBCASE("plaintext") { invalid = "local-secret-test-value"; }
    auto result = protector.unseal(invalid);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::StorageFailure);
    CHECK(result.error().message.find("local-secret-test-value") == std::string::npos);
#endif
}
