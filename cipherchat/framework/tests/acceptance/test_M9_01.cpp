#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "update_fixture.hpp"
using namespace ccfw;
namespace fs=std::filesystem;
TEST_CASE("M9-01 real TLS delta download, atomic activation and rollback") {
    test::UpdateFixture f; auto fetch=updates::remoteFetcher(f.current);
    CHECK(updates::check(f.current,fetch)==f.latest);
    CHECK_THROWS(fetch("../server.db")); CHECK_THROWS(fetch("blobs/not-a-hash"));
    const auto staged=updates::stage(f.current,fetch);
    CHECK(staged.at("downloaded")==updates::blockSize+8); CHECK(staged.at("reused").get<std::uint64_t>()>updates::blockSize*2);
    CHECK(updates::versionPath(f.current)==".");
    CHECK(updates::stage(f.current,fetch).at("downloaded")==0);
    updates::activate(f.current,"1.1"); CHECK(updates::versionPath(f.current)==".updates/versions/1.1");
    CHECK(updates::installationRoot(f.current/".updates/versions/1.1")==f.current);
    CHECK_THROWS(updates::stage(f.current,fetch));
    updates::rollback(f.current); CHECK(updates::versionPath(f.current)==".");
    CHECK(fs::file_size(f.current/"user-data.txt")==22);
    CHECK(fs::file_size(f.current/"cipherchat.exe")==updates::blockSize*2);
    // Rejected updates cannot affect chat on a separate authenticated connection.
    test::RealClient alice("update_alice"),bob("update_bob"); alice.registerAndLogin(f.server.options()); bob.registerAndLogin(f.server.options());
    alice.trust(bob); bob.trust(alice); const auto sent=alice.chat.sendText({"update_bob","chat remains available"}); REQUIRE(sent); alice.settle(bob,sent.value());
}
TEST_CASE("M9-01 interrupted download reuses authenticated cache and rejects corrupted blocks") {
    test::UpdateFixture f; auto fetch=updates::remoteFetcher(f.current); int fetched=0;
    CHECK_THROWS(updates::stage(f.current,[&](const std::string& path) { if(path.starts_with("blobs/") && ++fetched==2) throw std::runtime_error("injected disconnect"); return fetch(path); }));
    CHECK(updates::versionPath(f.current)==".");
    CHECK(updates::stage(f.current,fetch).at("downloaded")==8);
    test::UpdateFixture broken; auto bad=updates::remoteFetcher(broken.current);
    CHECK_THROWS(updates::stage(broken.current,[&](const std::string& path) { auto data=bad(path); if(path.starts_with("blobs/")) data[0]^=1; return data; }));
    CHECK(updates::versionPath(broken.current)=="."); CHECK_THROWS(updates::activate(broken.current,"1.1"));
    CHECK(updates::stage(broken.current,bad).at("downloaded")==updates::blockSize+8);
    updates::writeAtomic(broken.current/".updates/versions/1.1/cipherchat.exe","corrupt");
    CHECK_THROWS(updates::activate(broken.current,"1.1")); CHECK(updates::versionPath(broken.current)==".");
}
TEST_CASE("M9-01 untrusted certificate, manifest paths and downgrade fail closed") {
    test::UpdateFixture f; auto cfg=updates::readJson(f.current/"update-config.json"); cfg["server_name"]="wrong-host";
    updates::writeAtomic(f.current/"update-config.json",cfg.dump()); CHECK_THROWS(updates::remoteFetcher(f.current));
    cfg["server_name"]="localhost"; updates::writeAtomic(f.current/"update-config.json",cfg.dump());
    const auto wrong=cc::net::makeMemoryServerTls(); updates::writeAtomic(f.current/"server-cert.pem",wrong.caCertPem); CHECK_THROWS(updates::remoteFetcher(f.current));
    updates::writeAtomic(f.current/"server-cert.pem",f.original.at("server-cert.pem")); auto fetch=updates::remoteFetcher(f.current);
    auto qtAssets=f.latest; auto imageAsset=qtAssets["files"][0];
    imageAsset["path"]="qml/indicator@2x.png"; qtAssets["files"].push_back(imageAsset);
    CHECK_NOTHROW(updates::validate(qtAssets));
    for (const auto* path : {"../escape","C:/escape","a/../../b","a\\b","con.dll","a.","a//b",".updates/evil","cipherchat-launcher.exe"}) {
        auto invalid=f.latest; invalid["files"][0]["path"]=path; CHECK_THROWS(updates::validate(invalid));
    }
    auto invalid=f.latest; invalid["files"].push_back(invalid["files"][0]); CHECK_THROWS(updates::validate(invalid));
    invalid=f.latest; invalid["files"][0]["size"]=-1; CHECK_THROWS(updates::validate(invalid));
    f.latest["sequence"]=std::uint64_t(1); f.publish(); CHECK_THROWS(updates::check(f.current,fetch));
    f.latest=f.oldManifest; f.publish(); CHECK_NOTHROW(updates::check(f.current,fetch)); CHECK_THROWS(updates::stage(f.current,fetch));
    f.latest["sequence"]=std::uint64_t(0); f.publish(); CHECK_THROWS(updates::check(f.current,fetch));
}
