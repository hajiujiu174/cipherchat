#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "application_fixture.hpp"
using namespace cc::proto;
using namespace ccfw;
TEST_CASE("UID authenticated lookup returns permanent database IDs without modifying identities") {
    test::AppFixture f; test::User bob; bob.save(f.database,"bob");
    const auto a=f.database.findAccount("alice"), b=f.database.findAccount("bob"); REQUIRE(a); REQUIRE(b);
    CHECK(a->id>0); CHECK(b->id>a->id); CHECK(f.account.userId()==a->id); CHECK(f.account.uidLookupSupported());
    auto query=withV2(makeLookupUid("bob")); query.msgId="uid-query"; f.router.onMessage(f.net.peer,query);
    REQUIRE(f.net.peer.inbox.size()==1); CHECK(f.net.peer.inbox.back().type==MsgType::UidResult);
    CHECK(f.net.peer.inbox.back().msgId==query.msgId); CHECK(bodyInt(f.net.peer.inbox.back().body,"uid")==b->id);
    REQUIRE(f.app.tick()); REQUIRE(f.sink.events.ofType<UserUidEvent>().size()==1);
    CHECK(f.sink.events.ofType<UserUidEvent>().back().username=="bob");
    REQUIRE(f.app.lookupUid("missing_user")); REQUIRE(f.app.tick()); CHECK(f.sink.events.ofType<UserUidEvent>().back().uid==0);
    auto invalid=withV2(makeLookupUid("bob")); invalid.body["username"]=12; f.router.onMessage(f.net.peer,invalid);
    CHECK(f.net.peer.inbox.back().type==MsgType::Error); f.net.peer.inbox.clear();
    test::Peer anonymous; f.router.onMessage(anonymous,withV2(makeLookupUid("alice"))); REQUIRE_FALSE(anonymous.inbox.empty()); CHECK(anonymous.inbox.back().type==MsgType::Error);
    const auto same=f.database.findAccount("bob"); REQUIRE(same); CHECK(same->id==b->id); CHECK(same->signPub==b->signPub);
    f.app.shutdown(); CHECK(f.account.userId()==0); CHECK_FALSE(f.account.uidLookupSupported()); CHECK_FALSE(f.app.lookupUid("bob"));
}
TEST_CASE("UID database identifiers survive reopening and are not reused for new accounts") {
    const auto path=(test::tempDirectory("UID-persistence")/"server.db").string(); std::int64_t old=0;
    { cc::server::Storage db(path); test::User user; user.save(db,"original"); old=db.findAccount("original")->id; }
    { cc::server::Storage db(path); CHECK(db.findAccount("original")->id==old); test::User user; user.save(db,"new_account"); CHECK(db.findAccount("new_account")->id>old); }
}
TEST_CASE("UID real TLS reconnect preserves IDs and encrypted delivery") {
    test::RealServer server; test::RealClient a("uid_alice"), b("uid_bob"); a.registerAndLogin(server.options()); b.registerAndLogin(server.options());
    const auto id=server.database.findAccount(a.name)->id; CHECK(a.account.userId()==id); CHECK(b.account.userId()!=id);
    REQUIRE(a.application.lookupUid(b.name)); REQUIRE(test::eventually([&]{a.application.tick();return !a.events.ofType<UserUidEvent>().empty();}));
    CHECK(a.events.ofType<UserUidEvent>().back().uid==server.database.findAccount(b.name)->id);
    a.trust(b); b.trust(a); const auto sent=a.chat.sendText({b.name,"UID keeps end-to-end encryption",0,{}}); REQUIRE(sent); a.settle(b,sent.value());
    a.application.shutdown(); CHECK(a.account.userId()==0); a.connect(server.options()); a.login(); CHECK(a.account.userId()==id);
}
TEST_CASE("UID capability is optional and older server login remains usable") {
    test::AppFixture f; f.app.shutdown(); REQUIRE(f.net.connect({})); f.account.connectionChanged(true); REQUIRE(f.account.negotiateProtocol());
    REQUIRE(f.net.peer.inbox.size()==1); f.net.peer.inbox.front().body.erase("uid_lookup"); REQUIRE(test::pumpAccount(f.account,f.net)); CHECK_FALSE(f.account.uidLookupSupported());
    REQUIRE(f.account.login("alice","password123"));
    // Process the real nonce proof, then emulate the old optional-field-free result.
    auto challenge=f.net.poll(); REQUIRE(challenge); REQUIRE(challenge.value()); REQUIRE(f.account.handle(*challenge.value()));
    for(auto& response:f.net.peer.inbox) if(response.type==MsgType::LoginResult) response.body.erase("user_id");
    REQUIRE(test::pumpAccount(f.account,f.net)); CHECK(f.account.phase()==AccountPhase::Online); CHECK(f.account.userId()==0);
    const auto count=f.net.sent; const auto result=f.app.lookupUid("bob"); CHECK_FALSE(result); CHECK(result.error().code==ErrorCode::NotImplemented); CHECK(f.net.sent==count);
}
