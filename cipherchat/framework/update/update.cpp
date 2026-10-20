#include "update.hpp"
#include "ccfw/transport.hpp"
#include <fstream>
#include <set>
#include <thread>
#include <cctype>
#include <memory>
#include <future>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "windows_process.hpp"
#endif
namespace ccfw::updates {
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string bytes(const fs::path& path, std::uint64_t limit = 1048576) {
    require(fs::file_size(path) <= limit, "文件超出允许大小");
    std::ifstream input(path, std::ios::binary); require(bool(input), "无法读取更新文件");
    return {std::istreambuf_iterator<char>(input), {}};
}
bool hex(const std::string& s) { return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos; }
bool version(const std::string& s) { return !s.empty() && s.size() <= 48 && s.front() != '.' && s.back() != '.' && s.find("..") == std::string::npos && s.find_first_not_of("0123456789.-") == std::string::npos; }
void safePath(const std::string& s) {
    require(!s.empty() && s.size() <= 220 && s.front() != '/' && s.find_first_of("\\:<>\"|?*\r\n") == std::string::npos, "更新路径不合法");
    // Qt's high-DPI image assets use names such as indicator@2x.png.
    require(s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/+@") == std::string::npos, "更新路径包含不支持的字符");
    std::size_t at = 0;
    while (at < s.size()) {
        auto end = s.find('/', at); auto part = s.substr(at, end == std::string::npos ? end : end - at);
        require(!part.empty() && part != "." && part != ".." && part.back() != '.', "更新路径不能穿越目录");
        auto base = part.substr(0, part.find('.')); for (char& c : base) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool numbered = base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '1' && base[3] <= '9';
        require(base != "con" && base != "prn" && base != "aux" && base != "nul" && !numbered, "更新路径为保留名称");
        if (end == std::string::npos) break;
        at = end + 1; require(at < s.size(), "更新文件路径不能以斜线结尾");
    }
}
void noLinks(const fs::path& path) {
    auto absolute = fs::absolute(path); fs::path current;
    for (const auto& part : absolute) {
        current /= part;
#ifdef _WIN32
        const auto attrs = GetFileAttributesW(current.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES && (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) continue;
        require(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "更新目录不能包含链接或重解析点");
#else
        require(!fs::is_symlink(current), "更新目录不能包含符号链接");
#endif
    }
}
void verifyFile(const fs::path& file, const Json& item) {
    noLinks(file); require(fs::is_regular_file(file) && fs::file_size(file) == item.at("size").get<std::uint64_t>(), "更新文件大小不一致");
    std::ifstream input(file, std::ios::binary);
    for (const auto& block : item.at("blocks")) {
        std::string value(block.at("size").get<std::size_t>(), '\0');
        require(bool(input.read(value.data(), static_cast<std::streamsize>(value.size()))), "更新文件不完整");
        require(digest(value) == block.at("sha256").get<std::string>(), "更新文件校验失败");
    }
}
void verifyDirectory(const fs::path& directory, const Json& manifest) {
    validate(manifest); for (const auto& item : manifest.at("files")) verifyFile(directory / item.at("path").get<std::string>(), item);
}
struct BlockLocation { fs::path file; std::uint64_t offset; std::size_t size; };
void writeCache(const fs::path& file, const std::string& value) {
    noLinks(file); fs::create_directories(file.parent_path()); auto temporary = file; temporary += ".tmp"; noLinks(temporary);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(value.data(), static_cast<std::streamsize>(value.size())); output.flush();
        require(bool(output), "无法保存下载缓存");
    }
    // Cache is disposable and rechecked on resume; do not force a disk flush
    // for every 256 KiB. Version pointers still use durable writeAtomic.
#ifdef _WIN32
    require(MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING), "无法保存下载缓存");
#else
    fs::rename(temporary,file);
#endif
}
class DownloadPool {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
public:
    explicit DownloadPool(unsigned count) {
        try { for (unsigned n = 0; n < count; ++n) workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> job;
                { std::unique_lock lock(mutex_); wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                  if (stopping_) return; job = std::move(jobs_.front()); jobs_.pop_front(); }
                job();
            }
        }); } catch (...) {
            { std::lock_guard lock(mutex_); stopping_ = true; }
            wake_.notify_all(); for (auto& worker : workers_) worker.join(); throw;
        }
    }
    ~DownloadPool() {
        { std::lock_guard lock(mutex_); stopping_ = true; jobs_.clear(); }
        wake_.notify_all(); for (auto& worker : workers_) worker.join();
    }
    std::shared_future<std::string> submit(std::function<std::string()> task) {
        auto work = std::make_shared<std::packaged_task<std::string()>>(std::move(task)); auto future = work->get_future().share();
        { std::lock_guard lock(mutex_); jobs_.push_back([work] { (*work)(); }); }
        wake_.notify_one(); return future;
    }
};
Json transferFiles(const Json& files, const fs::path& destination, const fs::path& root,
                   const std::string& group, const std::map<std::string,BlockLocation>& reusable,
                   const Fetch& fetch, const std::function<void(const Json&)>& progress, unsigned concurrency) {
    struct Target { fs::path file, temporary; std::size_t remaining; };
    struct Work { std::size_t file, size; std::uint64_t offset; std::string hash; };
    struct Pending { Work work; std::string value; bool downloaded = false; std::shared_future<std::string> future; };
    std::vector<Target> targets; std::vector<Work> blocks;
    std::uint64_t downloaded = 0, reused = 0, completed = 0, total = 0;
    auto commit = [](const Target& target) {
#ifdef _WIN32
        require(MoveFileExW(target.temporary.c_str(),target.file.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "无法完成更新文件");
#else
        fs::rename(target.temporary,target.file);
#endif
    };
    for (const auto& item : files) {
        const auto name = item.at("path").get<std::string>();
        Target target{destination/name,root/".updates"/"parts"/group/name,item.at("blocks").size()};
        noLinks(target.file); noLinks(target.temporary);
        fs::create_directories(target.file.parent_path()); fs::create_directories(target.temporary.parent_path());
        { std::ofstream out(target.temporary,std::ios::binary | std::ios::trunc); require(bool(out), "无法写入更新目录，请移动到可写文件夹"); }
        std::uint64_t offset = 0; total += item.at("size").get<std::uint64_t>();
        for (const auto& block : item.at("blocks")) {
            const auto size = block.at("size").get<std::size_t>();
            blocks.push_back({targets.size(),size,offset,block.at("sha256").get<std::string>()}); offset += size;
        }
        if (!target.remaining) commit(target);
        targets.push_back(std::move(target));
    }
    std::deque<Pending> pending; std::map<std::string,std::shared_future<std::string>> inFlight; std::size_t next = 0;
    std::unique_ptr<DownloadPool> pool; if (concurrency > 1) pool = std::make_unique<DownloadPool>(concurrency);
    // A shared, bounded window crosses file boundaries. Ready blocks can be
    // written at their own offsets while an earlier request reconnects.
    const auto window = concurrency == 1 ? 1 : concurrency * 4;
    while (next < blocks.size() || !pending.empty()) {
        while (next < blocks.size() && pending.size() < window) {
            Pending p; p.work = blocks[next++]; const auto hash = p.work.hash; const auto size = p.work.size;
            const auto cache = root/".updates"/"cache"/hash; noLinks(cache);
            if (fs::is_regular_file(cache) && fs::file_size(cache) == size) { auto found = bytes(cache,blockSize); if (digest(found) == hash) p.value = std::move(found); }
            if (p.value.empty() && reusable.count(hash)) {
                const auto& source = reusable.at(hash); std::ifstream input(source.file,std::ios::binary); input.seekg(static_cast<std::streamoff>(source.offset));
                std::string found(size,'\0'); if (input.read(found.data(),static_cast<std::streamsize>(size)) && digest(found) == hash) p.value = std::move(found);
            }
            if (p.value.empty()) {
                auto download = [&fetch, size, hash, cache] { auto value = fetch("blobs/"+hash); require(value.size() == size && digest(value) == hash,"下载块校验失败，旧版本未修改"); writeCache(cache,value); return value; };
                if (inFlight.count(hash)) p.future = inFlight.at(hash);
                else { p.downloaded = true; if (pool) { p.future = pool->submit(download); inFlight[hash] = p.future; } else p.value = download(); }
            }
            pending.push_back(std::move(p));
        }
        auto ready = std::find_if(pending.begin(),pending.end(),[](const auto& p) { return !p.future.valid() || p.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
        if (ready == pending.end()) { pending.front().future.wait_for(std::chrono::milliseconds(5)); continue; }
        auto p = std::move(*ready); pending.erase(ready); if (p.future.valid()) p.value = p.future.get(); inFlight.erase(p.work.hash);
        auto& target = targets[p.work.file];
        {
            std::fstream output(target.temporary,std::ios::binary | std::ios::in | std::ios::out); output.seekp(static_cast<std::streamoff>(p.work.offset));
            output.write(p.value.data(),static_cast<std::streamsize>(p.work.size)); output.flush(); require(bool(output),"更新写入失败");
            output.close(); require(bool(output),"更新写入失败");
        }
        if (!--target.remaining) commit(target);
        if (p.downloaded) downloaded += p.work.size; else reused += p.work.size;
        completed += p.work.size;
        if (progress) progress({{"event","progress"},{"completed",completed},{"total",total},{"downloaded",downloaded},{"reused",reused},{"connections",concurrency}});
    }
    return {{"downloaded",downloaded},{"reused",reused},{"total",total}};
}
#ifdef _WIN32
fs::path systemTar() {
    wchar_t directory[MAX_PATH]; const auto length = GetSystemDirectoryW(directory,MAX_PATH);
    return length && length < MAX_PATH ? fs::path(directory)/"tar.exe" : fs::path{};
}
std::string runTar(const std::vector<std::wstring>& args, const fs::path& log, const std::function<void(const Json&)>& progress) {
    const auto exe = systemTar(); noLinks(log);
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    struct Handles {
        HANDLE output = INVALID_HANDLE_VALUE, job = nullptr, process = nullptr, thread = nullptr;
        ~Handles() { if (job) CloseHandle(job); if (thread) CloseHandle(thread); if (process) CloseHandle(process); if (output != INVALID_HANDLE_VALUE) CloseHandle(output); }
    } handles;
    handles.output = CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(handles.output != INVALID_HANDLE_VALUE,"无法记录解压结果");
    handles.job = CreateJobObjectW(nullptr,nullptr); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(handles.job && SetInformationJobObject(handles.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"无法启动解压任务");
    std::wstring command = quote(exe.wstring()); for (const auto& arg : args) command += L" " + quote(arg);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = handles.output; startup.hStdError = handles.output;
    PROCESS_INFORMATION process{};
    require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW | CREATE_SUSPENDED,nullptr,nullptr,&startup,&process),"无法启动 Windows 内置解压程序");
    handles.process = process.hProcess; handles.thread = process.hThread;
    if (!AssignProcessToJobObject(handles.job,handles.process)) { TerminateProcess(handles.process,1); throw std::runtime_error("无法管理解压任务"); }
    require(ResumeThread(handles.thread) != DWORD(-1),"无法启动解压任务");
    DWORD result;
    while ((result = WaitForSingleObject(handles.process,1000)) == WAIT_TIMEOUT) if (progress) progress({{"event","extracting"}});
    DWORD exitCode = 1; require(result == WAIT_OBJECT_0 && GetExitCodeProcess(handles.process,&exitCode) && exitCode == 0,"压缩包解压失败，可继续下载重试");
    return bytes(log,1048576);
}
void extractArchive(const fs::path& archive, const fs::path& destination, const Json& names, const std::function<void(const Json&)>& progress) {
    std::set<std::string> expected; for (const auto& name : names) { expected.insert(name.get<std::string>()); noLinks(destination/name.get<std::string>()); }
    const auto listing = runTar({L"-tvf",archive.wstring()},archive.wstring()+L".list.log",progress);
    std::istringstream lines(listing); std::string line;
    while (std::getline(lines,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto at = line.find_last_of(' ');
        require(!line.empty() && line.front() == '-' && at != std::string::npos && expected.erase(line.substr(at+1)) == 1,"压缩包文件列表不符");
    }
    require(expected.empty(),"压缩包缺少文件");
    if (progress) progress({{"event","extracting"}});
    runTar({L"-xf",archive.wstring(),L"-C",destination.wstring()},archive.wstring()+L".extract.log",progress);
}
#endif
}
std::string digest(const std::string& value) {
    const auto binary = cc::crypto::sha256(value); constexpr char hexChars[] = "0123456789abcdef"; std::string out;
    for (unsigned char c : binary) { out += hexChars[c >> 4]; out += hexChars[c & 15]; } return out;
}
Json readJson(const fs::path& file) { noLinks(file); return Json::parse(bytes(file)); }
void writeAtomic(const fs::path& file, const std::string& value) {
    noLinks(file); fs::create_directories(file.parent_path()); auto tmp = file; tmp += ".tmp"; noLinks(tmp);
    { std::ofstream output(tmp, std::ios::binary | std::ios::trunc); require(bool(output.write(value.data(), static_cast<std::streamsize>(value.size()))), "无法保存更新文件"); output.flush(); require(bool(output), "无法保存更新文件"); }
#ifdef _WIN32
    require(MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "无法切换更新文件");
#else
    fs::rename(tmp, file);
#endif
}
void validate(const Json& manifest) {
    const auto product=manifest.value("product",std::string{"cipherchat"});
    require(product=="cipherchat"||product=="messager", "不支持此更新产品");
    require(manifest.at("format") == 1 && manifest.at("platform") == "windows-x64" && manifest.at("block_size") == blockSize, "不支持此更新格式或平台");
    require(version(manifest.at("version").get<std::string>()), "版本号不合法");
    require(manifest.at("sequence").is_number_unsigned() && manifest.at("sequence").get<std::uint64_t>() > 0, "发布序号不合法");
    require(manifest.value("notes", std::string{}).size() <= 8192, "更新说明过长");
    // Qt Quick ships imported QML modules as individual files; keep the total
    // byte limit and all path/hash checks while allowing a complete native UI.
    const auto& files = manifest.at("files"); require(files.is_array() && !files.empty() && files.size() <= 2048, "更新文件数量不合法");
    std::set<std::string> paths; std::uint64_t total = 0;
    for (const auto& item : files) {
        auto path = item.at("path").get<std::string>(); safePath(path);
        for (char& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        require(path != ".updates" && !path.starts_with(".updates/") && path != "release.json" && path != "current.txt" && path != "previous.txt" && path != "cipherchat-launcher.exe", "更新路径占用保留文件");
        require(paths.insert(path).second, "更新路径重复");
        const auto size = item.at("size").get<std::uint64_t>(); require(size <= 256 * 1024 * 1024, "更新文件过大"); total += size;
        const auto& blocks = item.at("blocks"); require(blocks.is_array() && blocks.size() == (size + blockSize - 1) / blockSize, "更新分块数量不符");
        std::uint64_t remaining = size;
        for (const auto& block : blocks) {
            require(block.at("size") == std::min<std::uint64_t>(blockSize, remaining) && hex(block.at("sha256").get<std::string>()), "更新分块不合法"); remaining -= block.at("size").get<std::uint64_t>();
        }
    }
    require(total <= 512 * 1024 * 1024 && paths.count(product=="messager"?"messager.exe":"cipherchat.exe") && paths.count("cipherchat-updater.exe") && paths.count("update-config.json") && paths.count("server-cert.pem"), "更新不完整或总大小超限");
    for (const auto& path : paths) { auto pos = path.find('/'); while (pos != std::string::npos) { require(!paths.count(path.substr(0,pos)), "更新文件和目录冲突"); pos = path.find('/',pos+1); } }
    if (manifest.contains("archives")) {
        const auto& archives = manifest.at("archives"); require(archives.is_array() && archives.size() <= 8,"压缩更新描述非法");
        std::set<std::string> names; for (const auto& item : files) names.insert(item.at("path").get<std::string>());
        for (const auto& archive : archives) {
            require(archive.at("base_sequence").is_number_unsigned() && archive.at("size").is_number_unsigned(),"压缩更新描述非法");
            auto remaining = archive.at("size").get<std::uint64_t>();
            require(remaining && remaining <= 256*1024*1024 && archive.at("blocks").is_array() && archive.at("blocks").size() == (remaining+blockSize-1)/blockSize,"压缩包大小非法");
            for (const auto& block : archive.at("blocks")) {
                require(block.at("size") == std::min<std::uint64_t>(remaining,blockSize) && hex(block.at("sha256").get<std::string>()),"压缩包分块非法"); remaining -= block.at("size").get<std::uint64_t>();
            }
            std::set<std::string> unique; require(archive.at("files").is_array() && !archive.at("files").empty() && archive.at("files").size() <= files.size(),"压缩包文件列表非法");
            for (const auto& name : archive.at("files")) { const auto value = name.get<std::string>(); require(names.count(value) && unique.insert(value).second,"压缩包文件列表非法"); }
        }
    }
}
fs::path installationRoot(const fs::path& running) {
    const auto dir = fs::absolute(running).lexically_normal(); noLinks(dir);
    if (dir.parent_path().filename() == "versions" && dir.parent_path().parent_path().filename() == ".updates") return dir.parent_path().parent_path().parent_path();
    return dir;
}
std::string versionPath(const fs::path& root) {
    noLinks(root / "current.txt");
    if (!fs::exists(root / "current.txt")) return ".";
    const auto text = bytes(root / "current.txt", 100);
    require(text == "." || (text.starts_with(".updates/versions/") && version(text.substr(18))), "当前版本指针无效"); return text;
}
Json check(const fs::path& current, const Fetch& fetch) {
    const auto local = readJson(current / "release.json"); validate(local);
    const auto latest = Json::parse(fetch("latest.json")); validate(latest);
    require(latest.value("product",std::string{"cipherchat"})==local.value("product",std::string{"cipherchat"}),"更新渠道与客户端不匹配");
    require(latest.at("sequence").get<std::uint64_t>() >= local.at("sequence").get<std::uint64_t>(), "服务器版本比本机旧，已拒绝降级");
    if (latest.at("sequence") == local.at("sequence")) require(latest == local, "相同发布序号的内容不一致");
    return latest;
}
Json stage(const fs::path& current, const Fetch& fetch, const std::function<void(const Json&)>& progress, StageOptions options) {
    require(options.concurrency >= 1 && options.concurrency <= 32, "更新并发数量非法");
    const auto manifest = check(current, fetch); const auto root = installationRoot(current); const auto ver = manifest.at("version").get<std::string>();
    const auto local = readJson(current / "release.json");
    require(manifest.at("sequence").get<std::uint64_t>() > local.at("sequence").get<std::uint64_t>() && manifest.at("version") != local.at("version"), "当前已经是此版本，无需下载");
    const auto active = readJson(root / versionPath(root) / "release.json");
    require(manifest.at("sequence").get<std::uint64_t>() > active.at("sequence").get<std::uint64_t>(), "另一个客户端已切换版本，请重新启动");
    const auto destination = root / ".updates" / "versions" / ver; noLinks(destination);
    if (fs::exists(destination / "release.json")) {
        require(readJson(destination / "release.json") == manifest, "同一版本的发布内容发生变化，拒绝覆盖");
        verifyDirectory(destination, manifest);
        return {{"event","staged"},{"version",ver},{"downloaded",0},{"reused",0},{"total",0}};
    }
    fs::create_directories(destination);
    std::map<std::string, BlockLocation> reusable;
    for (const auto& item : local.at("files")) {
        const auto file = current / item.at("path").get<std::string>(); noLinks(file); std::uint64_t offset = 0;
        if (!fs::is_regular_file(file) || fs::file_size(file) != item.at("size").get<std::uint64_t>()) continue;
        for (const auto& block : item.at("blocks")) { reusable.emplace(block.at("sha256").get<std::string>(), BlockLocation{file,offset,block.at("size").get<std::size_t>()}); offset += block.at("size").get<std::size_t>(); }
    }
    std::uint64_t total = 0; for (const auto& item : manifest.at("files")) total += item.at("size").get<std::uint64_t>();
    require(fs::space(root).available > total*2 + blockSize*2, "磁盘空间不足，不能准备更新");
    auto remaining = manifest.at("files"); Json transferred;
#ifdef _WIN32
    auto missing = [&](const Json& block, bool useInstalled) -> std::uint64_t {
        const auto hash = block.at("sha256").get<std::string>(); const auto size = block.at("size").get<std::uint64_t>();
        if (useInstalled && reusable.count(hash)) return 0;
        const auto cache = root/".updates"/"cache"/hash;
        return fs::is_regular_file(cache) && fs::file_size(cache) == size ? 0 : size;
    };
    Json selected; std::uint64_t bestCost = 0;
    for (const auto& item : remaining) for (const auto& block : item.at("blocks"))
        bestCost += missing(block,true);
    if (options.compressed && manifest.contains("archives") && fs::is_regular_file(systemTar())) for (const auto& archive : manifest.at("archives")) {
        const auto base = archive.at("base_sequence").get<std::uint64_t>();
        if (base != 0 && base != local.at("sequence").get<std::uint64_t>()) continue;
        std::set<std::string> zipped; for (const auto& name : archive.at("files")) zipped.insert(name.get<std::string>());
        std::uint64_t cost = 0; for (const auto& block : archive.at("blocks")) cost += missing(block,false);
        for (const auto& item : remaining) if (!zipped.count(item.at("path").get<std::string>()))
            for (const auto& block : item.at("blocks")) cost += missing(block,true);
        if (cost < bestCost) { bestCost = cost; selected = archive; }
    }
    if (!selected.is_null()) {
        if (progress) progress({{"event","strategy"},{"mode","zip"},{"bytes",selected.at("size")},{"connections",options.concurrency}});
        const auto name = ver+"-"+std::to_string(selected.at("base_sequence").get<std::uint64_t>())+".zip";
        const auto directory = root/".updates"/"archives"; noLinks(directory); fs::create_directories(directory);
        auto item = selected; item["path"] = name;
        std::set<std::string> zipped; for (const auto& file : selected.at("files")) zipped.insert(file.get<std::string>());
        remaining = Json::array(); auto planTotal = selected.at("size").get<std::uint64_t>();
        for (const auto& file : manifest.at("files")) if (!zipped.count(file.at("path").get<std::string>())) { remaining.push_back(file); planTotal += file.at("size").get<std::uint64_t>(); }
        auto archiveProgress = [&](Json event) { event["total"] = planTotal; if (progress) progress(event); };
        transferred = transferFiles(Json::array({item}),directory,root,ver+"-archive",{},fetch,archiveProgress,options.concurrency);
        extractArchive(directory/name,destination,selected.at("files"),progress);
        for (const auto& file : manifest.at("files")) {
            if (zipped.count(file.at("path").get<std::string>())) require(fs::file_size(destination/file.at("path").get<std::string>()) == file.at("size").get<std::uint64_t>(),"解压文件不完整");
        }
    }
#endif
    if (transferred.is_null()) {
        if (progress) progress({{"event","strategy"},{"mode","blocks"},{"connections",options.concurrency}});
        transferred = transferFiles(remaining,destination,root,ver,reusable,fetch,progress,options.concurrency);
    } else {
        const auto offset = transferred;
        auto forward = [&](Json event) {
            event["completed"] = offset.at("total").get<std::uint64_t>() + event.at("completed").get<std::uint64_t>();
            event["total"] = offset.at("total").get<std::uint64_t>() + event.at("total").get<std::uint64_t>();
            event["downloaded"] = offset.at("downloaded").get<std::uint64_t>() + event.at("downloaded").get<std::uint64_t>();
            event["reused"] = offset.at("reused").get<std::uint64_t>() + event.at("reused").get<std::uint64_t>();
            if (progress) progress(event);
        };
        const auto rest = transferFiles(remaining,destination,root,ver,reusable,fetch,forward,options.concurrency);
        for (const auto* key : {"downloaded","reused"}) transferred[key] = transferred.at(key).get<std::uint64_t>() + rest.at(key).get<std::uint64_t>();
    }
    // Downloaded blocks / ZIP CRC are checked in the write/decode stream.
    // Activation checks final files once; no repeated whole-tree download scan.
    writeAtomic(destination / "release.json", manifest.dump(2));
    return {{"event","staged"},{"version",ver},{"downloaded",transferred.at("downloaded")},{"reused",transferred.at("reused")},{"total",total}};
}

void activate(const fs::path& root, const std::string& ver) {
    require(version(ver), "版本号不合法"); const std::string next = ".updates/versions/" + ver; const auto current = versionPath(root);
    const auto manifest = readJson(root / next / "release.json"); require(manifest.at("version") == ver, "版本目录不匹配");
    const auto prior = readJson(root / current / "release.json"); validate(prior);
    require(manifest.value("product",std::string{"cipherchat"})==prior.value("product",std::string{"cipherchat"}),"更新渠道与客户端不匹配");
    require(manifest.at("sequence").get<std::uint64_t>() > prior.at("sequence").get<std::uint64_t>(), "只能安装更新版本");
    verifyDirectory(root / next, manifest);
    writeAtomic(root / "previous.txt", current); writeAtomic(root / "current.txt", next);
}
void rollback(const fs::path& root) {
    noLinks(root / "previous.txt");
    const auto prior = bytes(root / "previous.txt",100); require(prior == "." || (prior.starts_with(".updates/versions/") && version(prior.substr(18))), "回退指针无效");
    verifyDirectory(root / prior, readJson(root / prior / "release.json")); writeAtomic(root / "current.txt", prior);
}
Fetch remoteFetcher(const fs::path& current, RemoteOptions options) {
    const auto config = readJson(current / "update-config.json");
    const auto channel=config.value("channel",std::string{});
    require(channel.empty()||channel=="messager","更新渠道无效");
    const auto prefix=channel.empty()?std::string{}:channel+"/";
    require(options.attempts >= 1 && options.attempts <= 6 && options.requestTimeoutMs >= 20 && options.requestTimeoutMs <= 90000 && options.connectTimeoutMs >= 20 && options.connectTimeoutMs <= 60000 && options.backoffMs <= 5000, "更新重试配置非法");
    struct Channel { std::mutex mutex; std::unique_ptr<ITransport> transport; };
    struct State { std::mutex mutex; std::map<std::thread::id, std::shared_ptr<Channel>> channels; std::atomic<unsigned> sequence{0}; };
    auto state = std::make_shared<State>(); const ConnectionOptions connection{config.at("host").get<std::string>(),config.at("port").get<std::uint16_t>(),current / "server-cert.pem",config.at("server_name").get<std::string>(),options.connectTimeoutMs};
    std::function<std::unique_ptr<ITransport>()> makeTransport = options.transportFactory;
    if (!makeTransport) makeTransport = [] { return std::make_unique<TlsTransport>(); };
    auto primary = std::make_shared<Channel>(); primary->transport = makeTransport(); auto connected = primary->transport->connect(connection);
    require(bool(connected), "无法连接更新服务器或证书验证失败"); state->channels[std::this_thread::get_id()] = primary;
    return [state, connection, options, makeTransport, prefix](const std::string& path) {
        std::shared_ptr<Channel> channel;
        { std::lock_guard lock(state->mutex); auto& entry = state->channels[std::this_thread::get_id()]; if (!entry) entry = std::make_shared<Channel>(); channel = entry; }
        std::lock_guard operation(channel->mutex);
        for (unsigned attempt = 1; attempt <= options.attempts; ++attempt) {
            if (!channel->transport) {
                channel->transport = makeTransport(); auto result = channel->transport->connect(connection);
                if (!result) {
                    channel->transport.reset();
                    require(result.error().code != ErrorCode::AuthenticationFailed && result.error().code != ErrorCode::InvalidArgument && result.error().code != ErrorCode::ProtocolError, "更新服务器证书验证或连接配置失败");
                }
            }
            bool retry = !channel->transport;
            const auto id = "update:" + std::to_string(++state->sequence);
            if (!retry) { auto sent = channel->transport->send({cc::proto::MsgType::UpdateGet,id,{{"path",prefix+path}}}); retry = !sent; }
            const auto began = std::chrono::steady_clock::now(), deadline = began + std::chrono::milliseconds(options.requestTimeoutMs); auto nextStatus = began + std::chrono::seconds(1);
            while (!retry && std::chrono::steady_clock::now() < deadline) {
                auto result = channel->transport->poll();
                if (!result) {
                    require(result.error().code == ErrorCode::NotConnected || result.error().code == ErrorCode::Timeout, "更新响应或连接验证失败"); retry = true; break;
                }
                if (result.value()) {
                    const auto& message = *result.value(); if (message.msgId != id) continue;
                    require(message.type == cc::proto::MsgType::UpdateResult && message.body.value("ok",false), "更新资源不存在或服务暂不可用");
                    std::string value; require(cc::base64Decode(message.body.at("data").get<std::string>(),value) && value.size() <= 1048576, "更新响应非法"); return value;
                }
                const auto now = std::chrono::steady_clock::now();
                if (options.status && now >= nextStatus) { options.status({{"event","waiting"},{"attempt",attempt},{"elapsedMs",std::chrono::duration_cast<std::chrono::milliseconds>(now-began).count()}}); nextStatus = now + std::chrono::seconds(1); }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (channel->transport) channel->transport->close(); channel->transport.reset();
            require(attempt < options.attempts, "更新连接多次无响应，已保留已验证的下载部分，请点击继续下载");
            if (options.status) options.status({{"event","retry"},{"attempt",attempt+1},{"attempts",options.attempts}});
            std::this_thread::sleep_for(std::chrono::milliseconds(options.backoffMs * attempt));
        }
        throw std::runtime_error("更新下载失败");
    };
}
}
