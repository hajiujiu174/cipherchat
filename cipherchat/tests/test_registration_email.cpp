#include <doctest/doctest.h>
#include "server/registration_email.hpp"
#include "server/storage/storage.hpp"
#include "common/auth/email.hpp"
#include <future>

TEST_CASE("Registration email codes bind account, expire, throttle and are single use") {
    std::int64_t now = 100000;
    std::string code;
    cc::server::RegistrationEmail mail([&](const auto&,const auto& value) { code=value;return std::string{}; },[&]{return now;});
    CHECK(cc::auth::normalizedEmail("User+test@QQ.COM") == "user+test@qq.com");
    CHECK(cc::auth::normalizedEmail("test@a..com").empty());
    CHECK(cc::auth::normalizedEmail("x@qq.com\r\nBcc:x@y.cn").empty());
    REQUIRE(mail.request("alice","Test@Example.test").ok);
    REQUIRE(code.size()==6);
    CHECK_FALSE(mail.request("alice","test@example.test").ok);
    CHECK_FALSE(mail.consume("bob","test@example.test",code).ok);
    CHECK_FALSE(mail.consume("alice","other@example.test",code).ok);
    CHECK_FALSE(mail.consume("alice","test@example.test",code=="000000"?"111111":"000000").ok);
    CHECK(mail.consume("alice","test@example.test",code).ok);
    CHECK_FALSE(mail.consume("alice","test@example.test",code).ok);
    now += 60000; REQUIRE(mail.request("alice","test@example.test").ok);
    for(int i=0;i<5;++i) CHECK_FALSE(mail.consume("alice","test@example.test","invalid").ok);
    CHECK_FALSE(mail.consume("alice","test@example.test",code).ok);
    now += 60000; REQUIRE(mail.request("alice","test@example.test").ok);
    now += 600000; CHECK_FALSE(mail.consume("alice","test@example.test",code).ok);
    REQUIRE(mail.request("alice","test@example.test").ok);
    now += 60000; REQUIRE(mail.request("alice","test@example.test").ok);
    now += 60000; CHECK_FALSE(mail.request("alice","test@example.test").ok);
}
TEST_CASE("SMTP failure creates no usable code and SMTP does not lock code verification") {
    cc::server::RegistrationEmail missing;
    CHECK_FALSE(missing.available()); CHECK_FALSE(missing.request("alice","a@example.test").ok);
    std::string failedCode;
    cc::server::RegistrationEmail failed([&](const auto&,const auto& value){ failedCode=value;return "failed"; });
    CHECK_FALSE(failed.request("alice","a@example.test").ok);
    CHECK_FALSE(failed.consume("alice","a@example.test",failedCode).ok);
    std::promise<void> entered, release;
    auto released=release.get_future().share();
    cc::server::RegistrationEmail slow([&](const auto&,const auto&){entered.set_value();released.wait();return std::string{};});
    auto send=std::async(std::launch::async,[&]{return slow.request("alice","a@example.test");});
    entered.get_future().wait();
    auto check=std::async(std::launch::async,[&]{return slow.consume("bob","other@example.test","000000");});
    const auto ready=check.wait_for(std::chrono::seconds(1));
    release.set_value(); // Always release before asserting, including a regression.
    CHECK(ready==std::future_status::ready); CHECK_FALSE(check.get().ok); CHECK(send.get().ok);
}
TEST_CASE("Account email binding is atomic and preserves existing account rows") {
    cc::server::Storage storage("");
    REQUIRE(storage.createAccount("legacy","hash","salt",10000,"sign","dh","spk","sig"));
    REQUIRE(storage.createAccount("alice","hash","salt",10000,"sign","dh","spk","sig","a@example.test"));
    CHECK_FALSE(storage.createAccount("bob","hash","salt",10000,"sign","dh","spk","sig","a@example.test"));
    CHECK_FALSE(storage.findAccount("bob"));
    CHECK(storage.registrationEmail("legacy").empty());
    CHECK(storage.registrationEmail("alice")=="a@example.test");
    CHECK(storage.findAccount("legacy")->passwordHash=="hash");
}
TEST_CASE("Resend invalidates the previous code and the global hourly budget resets") {
    std::int64_t now = 100000;
    std::string code;
    cc::server::RegistrationEmail mail([&](const auto&,const auto& value){code=value;return std::string{};},[&]{return now;});
    REQUIRE(mail.request("alice","a@example.test").ok);
    const auto oldCode=code;
    now+=60000; REQUIRE(mail.request("alice","a@example.test").ok);
    if (oldCode!=code) CHECK_FALSE(mail.consume("alice","a@example.test",oldCode).ok);
    CHECK(mail.consume("alice","a@example.test",code).ok);
    for (int i=0;i<18;++i) REQUIRE(mail.request("test","user"+std::to_string(i)+"@example.test").ok);
    CHECK_FALSE(mail.request("test","over@example.test").ok);
    now+=3600000; CHECK(mail.request("test","over@example.test").ok);
}

#include "server/router/router.hpp"
#include "common/auth/login_proof.hpp"
namespace {
struct ResetPeer : cc::server::Peer {
    std::string user;
    std::vector<cc::proto::Message> messages;
    const std::string& username() const override { return user; }
    void setUsername(std::string value) override { user=std::move(value); }
    void deliver(const cc::proto::Message& msg) override { messages.push_back(msg); }
    cc::proto::Message request(cc::server::Router& router,cc::proto::MsgType type,nlohmann::json body={}) {
        router.onMessage(*this,cc::proto::withV2({type,"",body})); return messages.back();
    }
};
}
TEST_CASE("Email password reset preserves identity, revokes sessions and challenges, rejects wrong purpose and replay") {
    using namespace cc; using namespace cc::proto;
    server::Storage db(""); server::Router router(db);
    auto identity=crypto::generateEd25519(); auto salt=crypto::randomBytes(16);
    const auto oldHash=crypto::pbkdf2Sha256("OldPassword!",salt,auth::kPbkdf2Iterations,32);
    REQUIRE(db.createAccount("reset_alice",oldHash,salt,auth::kPbkdf2Iterations,base64Encode(identity.pub),"dh","spk","sig","reset@example.test"));
    REQUIRE(db.createAccount("legacy",oldHash,salt,auth::kPbkdf2Iterations,base64Encode(identity.pub),"dh","spk","sig"));
    const auto original=*db.findAccount("reset_alice");
    std::string registerCode,resetCode;
    router.requireRegistrationEmail(std::make_shared<server::RegistrationEmail>([&](auto&,const auto& code){registerCode=code;return std::string{};}));
    router.enablePasswordReset(std::make_shared<server::RegistrationEmail>([&](auto&,const auto& code){resetCode=code;return std::string{};}));
    auto hello=[&](ResetPeer& p){CHECK(bodyBool(p.request(router,MsgType::Hello).body,"password_reset_email"));};
    auto login=[&](ResetPeer& p,const std::string& password) {
        auto challenge=p.request(router,MsgType::LoginRequest,{{"username","reset_alice"}});
        Bytes nonce,salt; REQUIRE(base64Decode(bodyStr(challenge.body,"nonce"),nonce)); REQUIRE(base64Decode(bodyStr(challenge.body,"salt"),salt));
        auto hash=crypto::pbkdf2Sha256(password,salt,bodyInt(challenge.body,"iterations"),32);
        return p.request(router,MsgType::LoginResponse,{{"username","reset_alice"},{"nonce",base64Encode(nonce)},{"proof",base64Encode(auth::deriveLoginProof(hash,nonce))},{"sig",base64Encode(crypto::ed25519Sign(identity.priv,nonce))}});
    };
    ResetPeer online,pending,recovery;hello(online);hello(pending);hello(recovery);
    REQUIRE(bodyBool(login(online,"OldPassword!").body,"ok"));
    auto challenge=pending.request(router,MsgType::LoginRequest,{{"username","reset_alice"}});
    CHECK_FALSE(bodyBool(recovery.request(router,MsgType::RegistrationCode,{{"username","reset_alice"},{"email","wrong@example.test"},{"purpose","password_reset"}}).body,"ok"));
    CHECK_FALSE(bodyBool(recovery.request(router,MsgType::RegistrationCode,{{"username","legacy"},{"email","reset@example.test"},{"purpose","password_reset"}}).body,"ok"));
    REQUIRE(bodyBool(recovery.request(router,MsgType::RegistrationCode,{{"username","reset_alice"},{"email","reset@example.test"}}).body,"ok"));
    auto reset=[&](const std::string& code){return recovery.request(router,MsgType::PasswordReset,{{"username","reset_alice"},{"email","reset@example.test"},{"password","NewPassword!"},{"email_code",code}});};
    CHECK_FALSE(bodyBool(reset(registerCode).body,"ok"));
    CHECK(db.findAccount("reset_alice")->passwordHash==oldHash);
    REQUIRE(bodyBool(recovery.request(router,MsgType::RegistrationCode,{{"username","reset_alice"},{"email","reset@example.test"},{"purpose","password_reset"}}).body,"ok"));
    CHECK_FALSE(bodyBool(reset("invalid").body,"ok"));
    REQUIRE(bodyBool(reset(resetCode).body,"ok"));
    CHECK_FALSE(router.isOnline("reset_alice"));CHECK(online.username().empty());
    CHECK_FALSE(bodyBool(reset(resetCode).body,"ok"));
    Bytes nonce;REQUIRE(base64Decode(bodyStr(challenge.body,"nonce"),nonce));
    auto stale=pending.request(router,MsgType::LoginResponse,{{"username","reset_alice"},{"nonce",base64Encode(nonce)},{"proof",base64Encode(auth::deriveLoginProof(oldHash,nonce))},{"sig",base64Encode(crypto::ed25519Sign(identity.priv,nonce))}});
    CHECK_FALSE(bodyBool(stale.body,"ok"));
    const auto after=*db.findAccount("reset_alice");
    CHECK(after.id==original.id);CHECK(after.signPub==original.signPub);CHECK(after.dhPub==original.dhPub);CHECK(after.signedPrekey==original.signedPrekey);CHECK(after.spkSig==original.spkSig);
    CHECK(after.salt!=original.salt);CHECK(db.registrationEmail("reset_alice")=="reset@example.test");
    ResetPeer oldLogin,newLogin;hello(oldLogin);hello(newLogin);
    CHECK_FALSE(bodyBool(login(oldLogin,"OldPassword!").body,"ok"));
    CHECK(bodyBool(login(newLogin,"NewPassword!").body,"ok"));
    CHECK(db.findAccount("legacy")->passwordHash==oldHash);
}
