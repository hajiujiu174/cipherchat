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
Json readJson(const std::filesystem::path& file);
void writeAtomic(const std::filesystem::path& file, const std::string& bytes);
std::string digest(const std::string& bytes);
void validate(const Json& manifest);
std::filesystem::path installationRoot(const std::filesystem::path& runningDirectory);
Json check(const std::filesystem::path& current, const Fetch& fetch);
Json stage(const std::filesystem::path& current, const Fetch& fetch, const std::function<void(const Json&)>& progress = {}, StageOptions options = {});
void activate(const std::filesystem::path& root, const std::string& version);
void rollback(const std::filesystem::path& root);
std::string versionPath(const std::filesystem::path& root);
Fetch remoteFetcher(const std::filesystem::path& current, RemoteOptions options = {});
}
