#pragma once
#include "ccfw/types.hpp"
#include "ccfw/transport.hpp"
#include <functional>
namespace ccfw::updates {
using Json = nlohmann::json;
using Fetch = std::function<std::string(const std::string&)>;
constexpr unsigned downloadConnections = 16;
struct StageOptions { unsigned concurrency = 1; bool compressed = true; };
struct RemoteOptions {
    unsigned attempts = 4;
    std::uint32_t requestTimeoutMs = 30000;
    std::uint32_t connectTimeoutMs = 10000;
    std::uint32_t backoffMs = 300;
    std::function<void(const Json&)> status;
    std::function<std::unique_ptr<ITransport>()> transportFactory;
};
constexpr std::size_t blockSize = 262144;
// 读取JSON。
Json readJson(const std::filesystem::path& file);
// 将新内容写到临时目标并原子提交，避免调用者读到半写入状态。
void writeAtomic(const std::filesystem::path& file, const std::string& bytes);
// 返回软件更新使用的摘要。
std::string digest(const std::string& bytes);
// 验证软件更新。
void validate(const Json& manifest);
// 返回软件更新使用的安装根。
std::filesystem::path installationRoot(const std::filesystem::path& runningDirectory);
// 检查软件更新。
Json check(const std::filesystem::path& current, const Fetch& fetch);
// 准备安装软件更新。
Json stage(const std::filesystem::path& current, const Fetch& fetch, const std::function<void(const Json&)>& progress = {}, StageOptions options = {});
// 启用软件更新。
void activate(const std::filesystem::path& root, const std::string& version);
// 回滚软件更新。
void rollback(const std::filesystem::path& root);
// 返回软件更新使用的版本路径。
std::string versionPath(const std::filesystem::path& root);
// 创建通过服务器传输读取更新清单与内容块的获取函数。
Fetch remoteFetcher(const std::filesystem::path& current, RemoteOptions options = {});
}
