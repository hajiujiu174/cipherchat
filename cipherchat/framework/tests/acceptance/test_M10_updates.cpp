#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "update_fixture.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#ifdef _WIN32
#include "../../update/windows_process.hpp"
#endif
using namespace ccfw;
namespace {
struct FaultState { std::atomic<int> connections{0}, drops{0}; bool disconnect = false; };
// Drops only an authenticated block reply, while preserving a real TLS
// connection underneath. A new connection receives the normal reply.
class MissingReply final : public ITransport {
    TlsTransport real_; std::shared_ptr<FaultState> state_; bool block_ = false, missing_ = false;
public:
    explicit MissingReply(std::shared_ptr<FaultState> state) : state_(std::move(state)) {}
    Result<> connect(const ConnectionOptions& options) override { ++state_->connections; return real_.connect(options); }
    Result<> send(const cc::proto::Message& message) override {
        block_ = message.body.value("path",std::string{}).starts_with("blobs/"); return real_.send(message);
    }
    Result<std::optional<cc::proto::Message>> poll() override {
        if (missing_) return Result<std::optional<cc::proto::Message>>::success(std::nullopt);
        auto result = real_.poll();
        if (result && result.value() && block_ && state_->drops.fetch_add(1) == 0) {
            if (state_->disconnect) { real_.close(); return Result<std::optional<cc::proto::Message>>::failure({ErrorCode::NotConnected,"M10-updates","injected close"}); }
            missing_ = true; return Result<std::optional<cc::proto::Message>>::success(std::nullopt);
        }
        return result;
    }
    void close() noexcept override { real_.close(); }
};
}
TEST_CASE("M10 updater automatically reconnects after an authenticated reply stalls or the socket closes") {
    for (bool disconnect : {false, true}) {
        test::UpdateFixture f; auto state = std::make_shared<FaultState>(); state->disconnect = disconnect;
        updates::RemoteOptions options; options.requestTimeoutMs = 1200; options.backoffMs = 1;
        options.transportFactory = [state] { return std::make_unique<MissingReply>(state); };
        std::atomic<int> retries{0}; options.status = [&](const auto& event) { if (event.value("event",std::string{}) == "retry") ++retries; };
        auto fetch = updates::remoteFetcher(f.current,options); const auto staged = updates::stage(f.current,fetch);
        CHECK(staged.at("downloaded") == updates::blockSize + 8); INFO("disconnect=" << disconnect << " retries=" << retries.load() << " connections=" << state->connections.load()); CHECK(retries == 1); CHECK(state->connections == 2);
        CHECK(updates::versionPath(f.current) == "."); updates::activate(f.current,"1.1"); updates::rollback(f.current);
    }
}
TEST_CASE("M10 parallel updater bounds four workers and resumes only authenticated blocks") {
    test::UpdateFixture f; auto changed = f.original; std::string payload;
    for (int i = 0; i < 64; ++i) payload += std::string(updates::blockSize,char('0'+i));
    changed["large.bin"] = payload;
    f.latest = f.manifest("1.2",3,changed); f.publish(); auto real = updates::remoteFetcher(f.current);
    std::atomic<int> active{0}, maximum{0}, fetched{0}; std::mutex idsMutex; std::set<std::thread::id> ids;
    auto fetch = [&](const std::string& path) {
        if (!path.starts_with("blobs/")) return real(path);
        const auto count = ++active; int prior = maximum; while (count > prior && !maximum.compare_exchange_weak(prior,count)) {}
        { std::lock_guard lock(idsMutex); ids.insert(std::this_thread::get_id()); }
        std::this_thread::sleep_for(std::chrono::milliseconds(30)); ++fetched;
        try { auto result = real(path); --active; return result; } catch (...) { --active; throw; }
    };
    CHECK_THROWS(updates::stage(f.current,fetch,[&](const auto& event) { if (event.at("completed").template get<std::uint64_t>() >= updates::blockSize * 4) throw std::runtime_error("controlled pause"); },{4}));
    CHECK(updates::versionPath(f.current) == "."); const auto before = fetched.load(); maximum = 0; ids.clear();
    const auto staged = updates::stage(f.current,fetch,{}, {4}); CHECK(maximum >= 2); CHECK(maximum <= 4); CHECK(ids.size() <= 4);
    CHECK(fetched-before < 64); CHECK(staged.at("reused").get<std::uint64_t>() >= updates::blockSize*4);
    CHECK(std::filesystem::file_size(f.current/".updates/versions/1.2/large.bin") == payload.size());
    updates::activate(f.current,"1.2"); updates::rollback(f.current);
}
TEST_CASE("M10 full speed window spans small files and shares duplicate blocks") {
    test::UpdateFixture f; auto changed = f.original;
    for (int i = 0; i < 32; ++i) changed["many/"+std::to_string(100+i)+".bin"] = std::string(4096,char('A'+i));
    changed["many/100-copy.bin"] = changed.at("many/100.bin"); changed["many/empty.bin"] = "";
    f.latest = f.manifest("1.3",4,changed); f.publish(); auto real = updates::remoteFetcher(f.current);
    std::atomic<int> active{0}, maximum{0}; std::mutex mutex; std::condition_variable gate; int arrivals = 0;
    std::map<std::string,int> requests;
    auto fetch = [&](const std::string& path) {
        if (!path.starts_with("blobs/")) return real(path);
        const auto count = ++active; int prior = maximum; while (count > prior && !maximum.compare_exchange_weak(prior,count)) {}
        {
            std::unique_lock lock(mutex); ++requests[path]; ++arrivals; gate.notify_all();
            gate.wait_for(lock,std::chrono::seconds(3),[&] { return arrivals >= 16; });
        }
        try { auto value = real(path); --active; return value; } catch (...) { --active; throw; }
    };
    const auto staged = updates::stage(f.current,fetch,{}, {updates::downloadConnections});
    CHECK(maximum == 16); CHECK(requests.size() == 32);
    for (const auto& [path,count] : requests) { INFO(path); CHECK(count == 1); }
    CHECK(staged.at("downloaded") == 32*4096);
    const auto directory = f.current/".updates/versions/1.3";
    for (const auto& [path,value] : changed) {
        std::ifstream input(directory/path,std::ios::binary); const std::string actual{std::istreambuf_iterator<char>(input),{}};
        INFO(path); CHECK(actual == value);
    }
    updates::activate(f.current,"1.3"); updates::rollback(f.current);
    CHECK_THROWS(updates::stage(f.current,fetch,{}, {33}));
}
TEST_CASE("M10 updater never retries identity failure") {
    test::UpdateFixture f; std::atomic<int> created{0}; auto config = updates::readJson(f.current/"update-config.json"); config["server_name"] = "wrong-host";
    updates::writeAtomic(f.current/"update-config.json",config.dump()); updates::RemoteOptions options;
    options.transportFactory = [&] { ++created; return std::make_unique<TlsTransport>(); };
    CHECK_THROWS(updates::remoteFetcher(f.current,options)); CHECK(created == 1);
}
#ifdef _WIN32
TEST_CASE("M10 optional real release ZIP activates and rolls back through TLS") {
    const auto configured = std::getenv("CCFW_UPDATE_FEED"); if (!configured) return;
    const auto feed = std::filesystem::path(configured); test::UpdateFixture f;
    _putenv_s("CIPHERCHAT_UPDATE_ROOT",feed.string().c_str());
    const auto latest = updates::readJson(feed/"latest.json"); bool compressed = false;
    const auto staged = updates::stage(f.current,updates::remoteFetcher(f.current),[&](const auto& event) {
        if (event.at("event") == "strategy") compressed = event.at("mode") == "zip";
    },{16});
    REQUIRE(compressed); CHECK(staged.at("downloaded").get<std::uint64_t>() < staged.at("total").get<std::uint64_t>()/2);
    const auto version = latest.at("version").get<std::string>(); updates::activate(f.current,version); updates::rollback(f.current);
    CHECK(updates::versionPath(f.current) == "."); CHECK(std::filesystem::file_size(f.current/"user-data.txt") == 22);
}
TEST_CASE("M10 compressed delta resumes its ZIP and extracts with the Windows runtime") {
    for (std::uint64_t base : {0u,1u}) {
        test::UpdateFixture f; const std::string payload(updates::blockSize*8,'Z'); auto changed = f.original; changed["new.bin"] = payload;
        f.latest = f.manifest("1.4",5,changed);
        const auto source = f.root/"zip-source"; std::filesystem::create_directories(source); updates::writeAtomic(source/"new.bin",payload);
        const auto archive = f.root/"test.zip";
        wchar_t directory[MAX_PATH]; REQUIRE(GetSystemDirectoryW(directory,MAX_PATH)); const auto tar = std::filesystem::path(directory)/"tar.exe";
        std::wstring command = updates::quote(tar.wstring())+L" -a -cf "+updates::quote(archive.wstring())+L" -C "+updates::quote(source.wstring())+L" new.bin";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        REQUIRE(CreateProcessW(tar.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
        const auto result = WaitForSingleObject(process.hProcess,10000); DWORD code = 1; GetExitCodeProcess(process.hProcess,&code); CloseHandle(process.hThread); CloseHandle(process.hProcess);
        REQUIRE(result == WAIT_OBJECT_0); REQUIRE(code == 0);
        std::ifstream input(archive,std::ios::binary); const std::string zip{std::istreambuf_iterator<char>(input),{}};
        REQUIRE(zip.size() < updates::blockSize); const auto hash = updates::digest(zip); updates::writeAtomic(f.feed/"blobs"/hash,zip);
        f.latest["archives"] = updates::Json::array({{{"base_sequence",base},{"size",zip.size()},{"blocks",updates::Json::array({{{"size",zip.size()},{"sha256",hash}}})},{"files",{"new.bin"}}}}); f.publish();
        auto fetch = updates::remoteFetcher(f.current);
        CHECK_THROWS(updates::stage(f.current,fetch,[](const auto& event) { if (event.at("event") == "progress") throw std::runtime_error("paused compressed download"); },{16}));
        const auto staged = updates::stage(f.current,fetch,{}, {16}); CHECK(staged.at("downloaded") == 0);
        std::ifstream extracted(f.current/".updates/versions/1.4/new.bin",std::ios::binary); const std::string actual{std::istreambuf_iterator<char>(extracted),{}}; CHECK(actual == payload);
        updates::activate(f.current,"1.4"); updates::rollback(f.current);
        auto invalid = f.latest; invalid["archives"][0]["files"].push_back("../outside"); CHECK_THROWS(updates::validate(invalid));
        invalid = f.latest; invalid["archives"][0]["blocks"][0]["size"] = 3; CHECK_THROWS(updates::validate(invalid));
    }
}
#endif
