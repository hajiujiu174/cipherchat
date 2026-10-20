#pragma once
#include "client_fixture.hpp"
#include "../../update/update.hpp"
#include <fstream>
namespace ccfw::test {
namespace fs = std::filesystem;
struct UpdateFixture {
    fs::path root = tempDirectory("update-test");
    fs::path current = root / "installed", feed = root / "feed";
    RealServer server;
    updates::Json oldManifest, latest;
    std::map<std::string,std::string> original;
    UpdateFixture() {
        fs::create_directories(current); fs::create_directories(feed / "blobs");
#ifdef _WIN32
        _putenv_s("CIPHERCHAT_UPDATE_ROOT",feed.string().c_str());
#else
        setenv("CIPHERCHAT_UPDATE_ROOT",feed.string().c_str(),1);
#endif
        std::ifstream cert(server.directory / "cert.pem",std::ios::binary); std::string pem{std::istreambuf_iterator<char>(cert),{}};
        original = {{"cipherchat.exe",std::string(updates::blockSize,'a') + std::string(updates::blockSize,'b')},
            {"cipherchat-updater.exe","helper"},{"server-cert.pem",pem},
            {"update-config.json",updates::Json{{"host","127.0.0.1"},{"port",server.tls.port()},{"server_name","localhost"}}.dump()},
            {"platforms/sample.dll",std::string(updates::blockSize,'q')}};
        oldManifest = manifest("1.0",1,original);
        for (const auto& [name,data] : original) updates::writeAtomic(current/name,data);
        updates::writeAtomic(current/"release.json",oldManifest.dump());
        updates::writeAtomic(current/"cipherchat-launcher.exe","fixture launcher");
        updates::writeAtomic(current/"user-data.txt","account data untouched");
        auto changed = original; changed["cipherchat.exe"].back() = 'c'; changed["new.txt"] = "new file";
        latest = manifest("1.1",2,changed); publish();
    }
    ~UpdateFixture() {
#ifdef _WIN32
        _putenv_s("CIPHERCHAT_UPDATE_ROOT","");
#else
        unsetenv("CIPHERCHAT_UPDATE_ROOT");
#endif
    }
    updates::Json manifest(std::string version, std::uint64_t sequence, const std::map<std::string,std::string>& files) {
        updates::Json out{{"format",1},{"platform","windows-x64"},{"block_size",updates::blockSize},{"version",version},{"sequence",sequence},{"notes","Test release"},{"files",updates::Json::array()}};
        for (const auto& [name,data] : files) {
            updates::Json item{{"path",name},{"size",data.size()},{"blocks",updates::Json::array()}};
            for (std::size_t at=0;at<data.size();at+=updates::blockSize) {
                auto value=data.substr(at,updates::blockSize); auto hash=updates::digest(value);
                updates::writeAtomic(feed/"blobs"/hash,value); item["blocks"].push_back({{"size",value.size()},{"sha256",hash}});
            }
            out["files"].push_back(item);
        }
        return out;
    }
    void publish() { updates::writeAtomic(feed/"latest.json",latest.dump()); }
};
}
