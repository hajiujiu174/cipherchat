#include <doctest/doctest.h>
#include "server/router/router.hpp"
#include "server/auth/auth.hpp"
#include "common/crypto/crypto.hpp"
#include "server/update_feed.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
using namespace cc::server;
using namespace cc::proto;
using Json=nlohmann::json;
namespace {
void users(Storage& db){for(auto name:{"alice","bob","mallory"})REQUIRE(db.createAccount(name,"hash","salt",1,"sign","dh","spk","sig"));}
struct ProfilePeer:Peer {std::string user;std::vector<Message> messages;void deliver(const Message& m)override{messages.push_back(m);}const std::string& username()const override{return user;}void setUsername(std::string s)override{user=std::move(s);}};
}
TEST_CASE("Messager update feed has an isolated anonymous channel and rejects traversal") {
    auto root=std::filesystem::temp_directory_path()/("update-channels-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"messager");std::ofstream(root/"latest.json")<<"legacy";std::ofstream(root/"messager/latest.json")<<"messager";
    const auto previous=std::getenv("CIPHERCHAT_UPDATE_ROOT");const std::string saved=previous?previous:"";
#ifdef _WIN32
    _putenv_s("CIPHERCHAT_UPDATE_ROOT",root.string().c_str());
#else
    setenv("CIPHERCHAT_UPDATE_ROOT",root.string().c_str(),1);
#endif
    auto response=[&](std::string path){return cc::server::updateResponse({MsgType::UpdateGet,"request",{{"path",path}}});};
    auto legacy=response("latest.json"),messager=response("messager/latest.json");std::string a,b;
    CHECK(legacy.body["ok"]==true);CHECK(messager.body["ok"]==true);CHECK(cc::base64Decode(legacy.body["data"].get<std::string>(),a));CHECK(cc::base64Decode(messager.body["data"].get<std::string>(),b));CHECK(a=="legacy");CHECK(b=="messager");
    for(auto path:{"../latest.json","messager/../latest.json","messager/../../server.db","other/latest.json","messager/blobs/../../latest.json","/latest.json"})CHECK(response(path).body["ok"]==false);
#ifdef _WIN32
    _putenv_s("CIPHERCHAT_UPDATE_ROOT",saved.c_str());
#else
    if(saved.empty())unsetenv("CIPHERCHAT_UPDATE_ROOT");else setenv("CIPHERCHAT_UPDATE_ROOT",saved.c_str(),1);
#endif
    std::filesystem::remove_all(root);
}
TEST_CASE("Profile moments persist, are newest first, paginate and enforce friendship") {
    auto directory=std::filesystem::current_path()/("profile-moments-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);auto path=(directory/"server.db").string();
    std::int64_t newest=0;
    {
        Storage db(path);users(db);
        auto png="iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aV6kAAAAASUVORK5CYII=";
        CHECK(db.socialRequest("alice",{{"op","save"},{"nickname","小林"},{"avatar",png}},100)["ok"]==true);
        CHECK_THROWS(db.socialRequest("bob",{{"op","save"},{"username","alice"},{"nickname","冒充"},{"avatar",""}},100));
        CHECK_THROWS(db.socialRequest("bob",{{"op","publish"},{"username","alice"},{"text","冒充"},{"request_id","forged"}},100));
        CHECK_THROWS(db.socialRequest("alice",{{"op","save"},{"nickname","昵称"},{"avatar","bm90LXBuZw=="}},100));
        CHECK_THROWS(db.socialRequest("alice",{{"op","publish"},{"text"," \n\t"},{"request_id","blank"}},100));
        CHECK_THROWS(db.socialRequest("alice",{{"op","publish"},{"text",std::string(2001,'x')},{"request_id","huge"}},100));
        for(int i=0;i<25;++i)newest=db.socialRequest("alice",{{"op","publish"},{"text","动态 "+std::to_string(i)},{"request_id","post-"+std::to_string(i)}},100)["id"].get<std::int64_t>();
        auto duplicate=db.socialRequest("alice",{{"op","publish"},{"text","动态 24"},{"request_id","post-24"}},500);CHECK(duplicate["id"]==newest);
        CHECK_THROWS(db.socialRequest("alice",{{"op","publish"},{"text","不同内容"},{"request_id","post-24"}},500));
        auto stranger=db.socialRequest("bob",{{"op","get"},{"username","alice"}},500);CHECK(stranger["nickname"]=="小林");CHECK(stranger["avatar"]==png);CHECK_FALSE(stranger["can_view"].get<bool>());CHECK(stranger["posts"].empty());CHECK(stranger["next"]==0);
        CHECK(db.requestFriend("bob","alice")=="pending");CHECK(db.socialRequest("bob",{{"op","get"},{"username","alice"}},500)["posts"].empty());
        REQUIRE(db.respondFriend("alice","bob",true));
        auto first=db.socialRequest("bob",{{"op","get"},{"username","alice"}},500);REQUIRE(first["posts"].size()==20);CHECK(first["posts"][0]["id"]==newest);CHECK(first["posts"][0]["text"]=="动态 24");
        for(unsigned i=1;i<20;++i)CHECK(first["posts"][i-1]["created_ms"].get<std::int64_t>()>first["posts"][i]["created_ms"].get<std::int64_t>());
        auto second=db.socialRequest("bob",{{"op","get"},{"username","alice"},{"before",first["next"]}},500);REQUIRE(second["posts"].size()==5);CHECK(second["next"]==0);CHECK(second["posts"][4]["text"]=="动态 0");
        CHECK(db.socialRequest("mallory",{{"op","get"},{"username","alice"},{"before",first["next"]}},500)["posts"].empty());
    }
    Storage reopened(path);auto result=reopened.socialRequest("alice",{{"op","get"}},600);CHECK(result["nickname"]=="小林");CHECK(result["posts"][0]["id"]==newest);CHECK(result["posts"].size()==20);
}
TEST_CASE("Profile moments router requires authentication and returns v2 capability and errors") {
    Storage db("");Router router(db);ProfilePeer peer;const auto signing=cc::crypto::generateEd25519();const auto key=cc::crypto::pbkdf2Sha256("password","salt",10000,32);
    REQUIRE(db.createAccount("alice",key,"salt",10000,cc::base64Encode(signing.pub),"dh","spk","sig"));
    router.onMessage(peer,makeHello());REQUIRE(!peer.messages.empty());CHECK(peer.messages.back().body["profile_moments"]==true);
    router.onMessage(peer,withV2({MsgType::SocialRequest,"anon",{{"op","get"},{"username","alice"}}}));CHECK(peer.messages.back().type==MsgType::Error);CHECK(peer.messages.back().body["code"]=="not_logged_in");
    router.onMessage(peer,withV2(makeLoginRequest("alice")));REQUIRE(peer.messages.back().type==MsgType::LoginChallenge);const auto challenge=peer.messages.back();cc::Bytes nonce;REQUIRE(cc::base64Decode(bodyStr(challenge.body,"nonce"),nonce));
    router.onMessage(peer,withV2(makeLoginResponse("alice",cc::base64Encode(cc::auth::deriveLoginProof(key,nonce)),cc::base64Encode(nonce),cc::base64Encode(cc::crypto::ed25519Sign(signing.priv,nonce)))));REQUIRE(peer.user=="alice");
    router.onMessage(peer,withV2({MsgType::SocialRequest,"publish",{{"op","publish"},{"text","Hello\n朋友圈"},{"request_id","wire-post"}}}));CHECK(peer.messages.back().type==MsgType::SocialResult);CHECK(peer.messages.back().body["ok"]==true);CHECK(isV2(peer.messages.back()));
    router.onMessage(peer,withV2({MsgType::SocialRequest,"bad",{{"op","get"},{"username","missing"}}}));CHECK(peer.messages.back().body["error"]=="not_found");
    router.onMessage(peer,withV2({MsgType::SocialRequest,"feed",{{"op","feed"},{"view_id","generation-1"}}}));CHECK(peer.messages.back().body["posts"].size()==1);CHECK(peer.messages.back().body["posts"][0]["username"]=="alice");
    router.onMessage(peer,withV2({MsgType::SocialRequest,"bad-feed",{{"op","feed"},{"before_ms",-1},{"view_id","generation-2"}}}));CHECK(peer.messages.back().body["error"]=="invalid_cursor");CHECK(peer.messages.back().body["view_id"]=="generation-2");
}

TEST_CASE("Moments feed globally orders self and every friend with stable composite pagination") {
    Storage db("");users(db);REQUIRE(db.createAccount("charlie","hash","salt",1,"sign","dh","spk","sig"));
    CHECK(db.requestFriend("alice","bob")=="pending");REQUIRE(db.respondFriend("bob","alice",true));
    CHECK(db.requestFriend("charlie","alice")=="pending");REQUIRE(db.respondFriend("alice","charlie",true));
    CHECK(db.requestFriend("mallory","alice")=="pending");
    for(int i=0;i<24;++i)db.socialRequest(i%2?"bob":"alice",{{"op","publish"},{"text","post-"+std::to_string(i)},{"request_id","feed-"+std::to_string(i)}},100+i/2);
    db.socialRequest("charlie",{{"op","publish"},{"text","older friend"},{"request_id","older"}},50);
    db.socialRequest("mallory",{{"op","publish"},{"text","private"},{"request_id","private"}},9999);
    db.socialRequest("bob",{{"op","save"},{"nickname","好友乙"},{"avatar",""}},200);
    auto first=db.socialRequest("alice",{{"op","feed"},{"username","mallory"},{"view_id","1"}},10000);
    REQUIRE(first["posts"].size()==20);CHECK(first["view_id"]=="1");CHECK(first["posts"][0]["text"]=="post-23");CHECK(first["posts"][0]["nickname"]=="好友乙");
    db.socialRequest("bob",{{"op","publish"},{"text","arrived while paging"},{"request_id","arrived"}},200);
    auto second=db.socialRequest("alice",{{"op","feed"},{"before_ms",first["next_ms"]},{"before_id",first["next_id"]}},10000);
    REQUIRE(second["posts"].size()==5);CHECK(second["next_id"]==0);CHECK(second["posts"][4]["username"]=="charlie");
    auto all=first["posts"];for(const auto& post:second["posts"])all.push_back(post);
    for(std::size_t i=0;i<all.size();++i){CHECK(all[i]["username"]!="mallory");if(i){auto a=all[i-1]["created_ms"].get<std::int64_t>(),b=all[i]["created_ms"].get<std::int64_t>();CHECK((a>b||(a==b&&all[i-1]["id"].get<std::int64_t>()>all[i]["id"].get<std::int64_t>())));}}
    auto privateFeed=db.socialRequest("mallory",{{"op","feed"}},10000);REQUIRE(privateFeed["posts"].size()==1);CHECK(privateFeed["posts"][0]["text"]=="private");
    auto fresh=db.socialRequest("alice",{{"op","feed"}},10000);CHECK(fresh["posts"][0]["text"]=="arrived while paging");
    CHECK_THROWS(db.socialRequest("alice",{{"op","feed"},{"before_ms",100},{"before_id",0}},10000));
    CHECK_THROWS(db.socialRequest("alice",{{"op","feed"},{"before_ms",0},{"before_id",1}},10000));
}
