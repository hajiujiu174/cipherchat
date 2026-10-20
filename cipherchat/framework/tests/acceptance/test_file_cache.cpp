#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "client_fixture.hpp"
#include "server/file_cache.hpp"
#include <fstream>
using namespace ccfw;
TEST_CASE("File cache persists ciphertext, enforces ownership, idempotence, expiry and quota") {
    const auto path=test::tempDirectory("cache-disk")/"files.sqlite3";const std::string bytes="CCF3opaque-encrypted-content";
    {
        cc::server::FileCache cache(1024,30);cache.open(path.string());
        CHECK(cache.begin("alice","id","bob",1,1,100).at("next")==0);
        CHECK(cache.put("alice","id",0,bytes,101)==1);CHECK(cache.put("alice","id",0,bytes,102)==1);
        CHECK_THROWS(cache.put("alice","id",0,"CCF3changed",103));CHECK_THROWS(cache.put("mallory","id",0,bytes,103));
        CHECK_THROWS(cache.begin("alice","id","mallory",1,1,103));
    }
    cc::server::FileCache cache(1024,30);cache.open(path.string());
    REQUIRE(cache.get("bob","alice","id",0,104));CHECK(*cache.get("bob","alice","id",0,104)==bytes);
    CHECK_THROWS(cache.get("mallory","alice","id",0,104));CHECK_THROWS(cache.get("bob","alice","id",0,131));
    CHECK(cache.begin("alice","id","bob",1,1,132).at("next")==0);
    cc::server::FileCache tiny(4,30);tiny.open("");tiny.begin("alice","id","bob",1,1,100);CHECK_THROWS(tiny.put("alice","id",0,bytes,101));
}
TEST_CASE("File cache real TLS: offline recipient, sender gone, restart recipient and download another copy") {
    auto server=std::make_unique<test::RealServer>();test::RealClient alice("cache_alice"),bob("cache_bob");
    alice.registerAndLogin(server->options());bob.registerAndLogin(server->options());alice.trust(bob);bob.trust(alice);
    bob.application.shutdown();
    const auto source=alice.directory/"cached.bin";std::string payload;
    for(int i=0;i<41;++i)payload+=std::string(65536,char(i));
    {std::ofstream out(source,std::ios::binary);out.write(payload.data(),payload.size());}
    const auto started=alice.files.start(bob.name,source);REQUIRE(started);const auto id=started.value();
    REQUIRE(test::eventually([&]{alice.application.tick();auto s=alice.store.load();return s && !s.value().transfers.empty() && s.value().transfers.back().cacheUploaded;},20000));
    CHECK_FALSE(alice.files.progress(id).value().complete);CHECK(alice.files.progress(id).value().transportPath==4);
    const auto saved=alice.store.load().value().transfers.back();const nlohmann::json legacyReadable=saved;CHECK(legacyReadable.at("transport_path")==3);
    alice.application.shutdown();
    const auto serverDirectory=server->directory;server.reset();server=std::make_unique<test::RealServer>(serverDirectory);
    bob.connect(server->options());bob.login();
    REQUIRE(test::eventually([&]{bob.application.tick();auto p=bob.files.progress(id);return p && p.value().complete;},20000));
    const auto first=bob.files.progress(id).value().path;
    auto read=[](const auto& path){std::ifstream in(path,std::ios::binary);return std::string{std::istreambuf_iterator<char>(in),{}};};CHECK(read(first)==payload);
    REQUIRE(bob.files.resume(id));
    REQUIRE(test::eventually([&]{bob.application.tick();auto p=bob.files.progress(id);return p && p.value().complete;},20000));
    const auto second=bob.files.progress(id).value().path;CHECK(first!=second);CHECK(read(first)==payload);CHECK(read(second)==payload);
    alice.connect(server->options());alice.login();
    REQUIRE(test::eventually([&]{alice.application.tick();bob.application.tick();return alice.files.progress(id).value().complete;},10000));
    for(const auto& error:bob.events.ofType<Error>()) { INFO(error.message);CHECK(error.code==ErrorCode::IdentityChanged); }
}
TEST_CASE("File cache resumes an interrupted uploader process and preserves its queued ciphertext") {
    test::RealServer server;auto alice=std::make_unique<test::RealClient>("resume_alice");test::RealClient bob("resume_bob");
    alice->registerAndLogin(server.options());bob.registerAndLogin(server.options());alice->trust(bob);bob.trust(*alice);bob.application.shutdown();
    const auto directory=alice->directory;const auto source=directory/"resume.bin";const std::string payload(4*1024*1024,'R');
    {std::ofstream out(source,std::ios::binary);out.write(payload.data(),payload.size());}
    const auto started=alice->files.start(bob.name,source);REQUIRE(started);const auto id=started.value();
    REQUIRE(test::eventually([&]{alice->application.tick();const auto p=alice->files.progress(id);return p && p.value().authenticatedBytes>=4*65536;},10000));
    const auto before=alice->files.progress(id).value().authenticatedBytes;REQUIRE(before<payload.size());
    alice.reset();alice=std::make_unique<test::RealClient>("resume_alice",directory);alice->connect(server.options());alice->login();
    REQUIRE(test::eventually([&]{alice->application.tick();return alice->files.progress(id).value().cacheUploaded;},20000));
    CHECK_FALSE(alice->files.progress(id).value().complete);alice.reset();bob.connect(server.options());bob.login();
    REQUIRE(test::eventually([&]{bob.application.tick();auto p=bob.files.progress(id);return p && p.value().complete;},20000));
    std::ifstream in(bob.files.progress(id).value().path,std::ios::binary);CHECK(std::string(std::istreambuf_iterator<char>(in),{})==payload);
}
TEST_CASE("File cache bounds requests while several downloads resume on a slow consumer") {
    test::RealServer server;test::RealClient alice("batch_alice"),bob("batch_bob");
    alice.registerAndLogin(server.options());bob.registerAndLogin(server.options());alice.trust(bob);bob.trust(alice);bob.application.shutdown();
    std::vector<std::string> ids;const std::string payload(21*65536,'B');
    for(int i=0;i<4;++i){auto source=alice.directory/("batch-"+std::to_string(i)+".bin");{std::ofstream out(source,std::ios::binary);out.write(payload.data(),payload.size());}auto started=alice.files.start(bob.name,source);REQUIRE(started);ids.push_back(started.value());}
    REQUIRE(test::eventually([&]{alice.application.tick();return std::all_of(ids.begin(),ids.end(),[&](auto& id){return alice.files.progress(id).value().cacheUploaded;});},30000));
    alice.application.shutdown();bob.connect(server.options());bob.login();
    REQUIRE(test::eventually([&]{bob.application.tick();std::this_thread::sleep_for(std::chrono::milliseconds(40));return std::all_of(ids.begin(),ids.end(),[&](auto& id){auto p=bob.files.progress(id);return p&&p.value().complete;});},40000));
    for(auto& id:ids){std::ifstream in(bob.files.progress(id).value().path,std::ios::binary);CHECK(std::string(std::istreambuf_iterator<char>(in),{})==payload);}
    for(const auto& error:bob.events.ofType<Error>()){INFO(error.message);CHECK(error.code==ErrorCode::IdentityChanged);}
}
