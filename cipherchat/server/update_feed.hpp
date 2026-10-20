#pragma once
#include "common/protocol/messages.hpp"
#include "common/base64.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace cc::server {
// Called on the requesting connection's IO thread, outside the chat router lock.
inline proto::Message updateResponse(const proto::Message& request) {
    proto::Message result{proto::MsgType::UpdateResult, request.msgId, {{"ok", false}}};
    try {
        const char* configured = std::getenv("CIPHERCHAT_UPDATE_ROOT");
        if (!configured || !*configured) return result;
        const auto path = request.body.at("path").get<std::string>();
        const auto relative=path.starts_with("messager/")?path.substr(9):path;
        const bool blob = relative.size() == 70 && relative.starts_with("blobs/") &&
            relative.find_first_not_of("0123456789abcdef", 6) == std::string::npos;
        if (relative != "latest.json" && !blob) return result;
        const auto root = std::filesystem::canonical(configured);
        auto file = root;
        for (const auto& part : std::filesystem::path(path)) {
            file /= part;
            if (std::filesystem::is_symlink(file)) return result;
        }
        const auto size = std::filesystem::file_size(file);
        if (size > (blob ? 262144u : 1048576u)) return result;
        std::ifstream input(file, std::ios::binary); std::string data(size, '\0');
        if (!input.read(data.data(), static_cast<std::streamsize>(size))) return result;
        result.body = {{"ok", true}, {"data", cc::base64Encode(data)}};
    } catch (...) { }
    return result;
}
}
