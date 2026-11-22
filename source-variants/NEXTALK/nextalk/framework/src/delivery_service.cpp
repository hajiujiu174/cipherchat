#include "ccfw/delivery_service.hpp"
#include "feature_codec.hpp"
#include "wire_helpers.hpp"
#include <openssl/evp.h>
#include <fstream>

namespace ccfw {
namespace {
Error invalid(ErrorCode c, const char* text) { return {c, "M10-07", text}; }
auto bundle(ClientSnapshot& s, const std::string& id) {
    return std::find_if(s.features.bundles.begin(), s.features.bundles.end(), [&](const auto& b) { return b.id == id; });
}
Result<> save(ILocalStore& store, ClientSnapshot& s) { auto old = s.revision++; return store.commit(old, s); }
std::string digestFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary); if (!in) throw std::runtime_error("file unavailable");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) throw std::runtime_error("digest failure");
    char block[65536];
    while (in.read(block, sizeof(block)) || in.gcount()) if (EVP_DigestUpdate(ctx.get(), block, static_cast<std::size_t>(in.gcount())) != 1) throw std::runtime_error("digest failure");
    if (!in.eof()) throw std::runtime_error("file read failure");
    cc::Bytes hash(32, '\0'); unsigned int count = 0;
    if (EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(hash.data()), &count) != 1 || count != 32) throw std::runtime_error("digest failure");
    return cc::base64Encode(hash);
}
}
double deliveryProgress(const DeliveryBundle& b) {
    std::uint64_t bytes = 0, total = 0; unsigned completed = 0;
    for (const auto& i : b.items) { bytes += std::min(i.size, i.authenticatedBytes); total += i.size; completed += i.complete; }
    return total ? static_cast<double>(bytes) / static_cast<double>(total) : b.items.empty() ? 0 : static_cast<double>(completed) / b.items.size();
}
Result<std::string> DeliveryService::create(const CreateDeliveryBundle& request) {
    auto loaded = store_.load(); if (!loaded) return Result<std::string>::failure(loaded.error()); auto s = loaded.value();
    if (!s.identity || request.operationId.empty() || request.operationId.size() > 64 || request.recipient.empty() || request.recipient.size() > 32 ||
        request.recipient == s.account.username || request.title.empty() || request.title.size() > 256 || request.description.size() > 4096 ||
        request.files.empty() || request.files.size() > 16 || s.features.bundles.size() >= 2048)
        return Result<std::string>::failure(invalid(ErrorCode::InvalidArgument, "交付需要标题、接收人和 1～16 个文件"));
    try {
        if (auto old = bundle(s, request.operationId); old != s.features.bundles.end()) {
            if (old->sender != s.account.username || old->recipient != request.recipient || old->title != request.title || old->description != request.description ||
                old->previousId != request.previousId || old->items.size() != request.files.size())
                return Result<std::string>::failure(invalid(ErrorCode::Conflict, "重复创建操作的清单不同，请创建新版本"));
            for (std::size_t n = 0; n < request.files.size(); ++n)
                if (s.features.deliverySources.at(old->items[n].id) != std::filesystem::canonical(request.files[n]))
                    return Result<std::string>::failure(invalid(ErrorCode::Conflict, "重复创建操作的源文件不同"));
            return Result<std::string>::success(old->id);
        }
        if (!request.previousId.empty()) {
            auto previous = bundle(s, request.previousId);
            if (previous == s.features.bundles.end() || previous->sender != s.account.username || previous->recipient != request.recipient)
                return Result<std::string>::failure(invalid(ErrorCode::Forbidden, "上一版本不属于此私聊交付"));
        }
        DeliveryBundle b; b.id = request.operationId; b.sender = s.account.username; b.recipient = request.recipient;
        b.title = request.title; b.description = request.description; b.previousId = request.previousId; b.createdAt = wire::now();
        for (const auto& p : request.files) {
            if (!std::filesystem::is_regular_file(p)) return Result<std::string>::failure(invalid(ErrorCode::InvalidArgument, "交付仅支持普通文件"));
            DeliveryItem i; i.id = wire::id(); i.transferId = wire::id(); i.name = codec::pathText(p.filename()); i.size = std::filesystem::file_size(p);
            if (i.size > 1024ULL * 1024 * 1024 || i.name.size() > 200) return Result<std::string>::failure(invalid(ErrorCode::InvalidArgument, "文件超过大小或名称限制"));
            s.features.deliverySources[i.id] = std::filesystem::canonical(p); b.items.push_back(std::move(i));
        }
        s.features.bundles.push_back(b); auto saved = save(store_, s); if (!saved) return Result<std::string>::failure(saved.error());
        events_.push_back(b); return Result<std::string>::success(b.id);
    } catch (...) { return Result<std::string>::failure(invalid(ErrorCode::StorageFailure, "无法读取交付源文件")); }
}
Result<> DeliveryService::tick() {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    // Fixed transfer IDs make a crash between ticks safe. startAssigned commits
    // the task and its bundle association in the same store transaction.
    for (const auto& b : loaded.value().features.bundles) if (b.sender == loaded.value().account.username && b.state != BundleState::Cancelled) {
        for (const auto& i : b.items) {
            if (!i.error.empty()) continue;
            if (std::any_of(loaded.value().transfers.begin(), loaded.value().transfers.end(), [&](const auto& t) { return t.fileId == i.transferId; })) continue;
            auto source = loaded.value().features.deliverySources.find(i.id); if (source == loaded.value().features.deliverySources.end()) continue;
            std::error_code sourceError;
            const bool sourceChanged = !std::filesystem::is_regular_file(source->second, sourceError) || std::filesystem::file_size(source->second, sourceError) != i.size || sourceError;
            auto started = sourceChanged ? Result<std::string>::failure(invalid(ErrorCode::InvalidState, "源文件变化或不可用，请恢复文件或创建新交付版本")) : files_.startAssigned(b.recipient, source->second, i.transferId, b.id, i.id);
            if (!started && started.error().code != ErrorCode::Forbidden) {
                auto current = store_.load(); if (!current) return Result<>::failure(current.error()); auto next = current.value(); auto affected = bundle(next, b.id);
                for (auto& item : affected->items) if (item.id == i.id) item.error = started.error().message;
                auto saved = save(store_, next); if (!saved) return saved;
            }
        }
    }
    loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto s = loaded.value();
    bool changed = false;
    for (auto& b : s.features.bundles) {
        const auto before = nlohmann::json(b).dump();
        for (auto& i : b.items) {
            auto t = std::find_if(s.transfers.begin(), s.transfers.end(), [&](const auto& v) { return v.fileId == i.transferId; });
            if (t == s.transfers.end()) continue;
            if (t->peer != (b.sender == s.account.username ? b.recipient : b.sender) || t->outgoing != (b.sender == s.account.username) || t->totalBytes != i.size) { i.error = "文件与清单不匹配"; continue; }
            if (b.announced && !t->sourceDigest.empty() && t->sourceDigest != i.sha256) { i.error = "文件摘要与清单不匹配"; continue; }
            if (!b.announced && !t->sourceDigest.empty()) i.sha256 = t->sourceDigest;
            i.authenticatedBytes = std::min(t->totalBytes, std::uint64_t(t->nextContiguousChunk) * 65536);
            i.complete = t->complete; i.cancelled = t->cancelled;
        }
        if (!b.announced && b.state != BundleState::Cancelled && std::all_of(b.items.begin(), b.items.end(), [](const auto& i) { return !i.sha256.empty() && i.error.empty(); })) {
            b.manifestDigest = features::digest(b); (void)features::parseManifest(features::manifest(b)); b.announced = true;
        }
        if (b.state != BundleState::Cancelled) b.state = b.manuallyConfirmed ? BundleState::Confirmed :
            std::all_of(b.items.begin(), b.items.end(), [](const auto& i) { return i.complete && i.error.empty(); }) ? BundleState::Complete : b.announced ? BundleState::Transferring : BundleState::Preparing;
        changed |= before != nlohmann::json(b).dump();
    }
    if (changed) { auto saved = save(store_, s); if (!saved) return saved; }
    for (const auto& b : s.features.bundles) {
        const auto state = nlohmann::json(b).dump();
        if (published_[b.id] != state) { published_[b.id] = state; events_.push_back(b); }
        if (b.announced && b.sender == s.account.username && b.state != BundleState::Cancelled) {
            const auto id = "delivery:" + b.id;
            if (!std::any_of(s.history.begin(), s.history.end(), [&](const auto& h) { return h.messageId == id; })) {
                SendText request{b.recipient, "文件交付：" + b.title + "\n" + b.description + "\n共 " + std::to_string(b.items.size()) + " 个文件", 0, {}};
                request.messageId = id; request.extensions = {{"delivery", {{"version", 1}, {"action", "manifest"}, {"manifest", features::manifest(b)}, {"digest", b.manifestDigest}}}};
                auto sent = chat_.sendText(request); if (!sent) return Result<>::failure(sent.error());
            }
        }
    }
    return Result<>::success({});
}
Result<> DeliveryService::resume(const ResumeDeliveryItem& request) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto s = loaded.value(); auto b = bundle(s, request.bundleId);
    if (b == s.features.bundles.end() || b->state == BundleState::Cancelled) return Result<>::failure(invalid(ErrorCode::NotFound, "交付不存在或已取消"));
    auto i = std::find_if(b->items.begin(), b->items.end(), [&](const auto& v) { return v.id == request.itemId; });
    if (i == b->items.end() || i->cancelled) return Result<>::failure(invalid(ErrorCode::InvalidState, "已取消的文件需要创建新交付版本"));
    if (std::none_of(s.transfers.begin(), s.transfers.end(), [&](const auto& t) { return t.fileId == i->transferId; })) {
        i->error.clear(); auto saved = save(store_, s); return saved ? tick() : saved;
    }
    auto restored = files_.resume(i->transferId);
    if (restored && !i->error.empty()) {
        loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); s = loaded.value(); b = bundle(s, request.bundleId);
        for (auto& item : b->items) if (item.id == request.itemId) item.error.clear(); return save(store_, s);
    }
    return restored;
}
Result<> DeliveryService::cancel(const CancelDeliveryBundle& request) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto s = loaded.value(); auto b = bundle(s, request.bundleId);
    if (b == s.features.bundles.end() || b->sender != s.account.username || b->manuallyConfirmed) return Result<>::failure(invalid(ErrorCode::Forbidden, "不能取消此交付"));
    const auto items = b->items;
    b->state = BundleState::Cancelled; auto saved = save(store_, s); if (!saved) return saved;
    for (const auto& i : items) {
        const auto t = std::find_if(s.transfers.begin(), s.transfers.end(), [&](const auto& v) { return v.fileId == i.transferId; });
        if (t != s.transfers.end() && !t->complete && !t->cancelled) { auto cancelled = files_.cancel(i.transferId); if (!cancelled) return cancelled; }
    }
    return tick();
}
Result<> DeliveryService::confirm(const ConfirmDelivery& request) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto s = loaded.value(); auto b = bundle(s, request.bundleId);
    if (b == s.features.bundles.end() || !b->announced || b->recipient != s.account.username || b->manifestDigest != request.manifestDigest)
        return Result<>::failure(invalid(ErrorCode::Forbidden, "交付接收人或清单摘要不匹配"));
    if (b->manuallyConfirmed) return Result<>::success({});
    try {
        for (const auto& i : b->items) {
            auto t = std::find_if(s.transfers.begin(), s.transfers.end(), [&](const auto& v) { return v.fileId == i.transferId; });
            if (t == s.transfers.end() || t->outgoing || t->peer != b->sender || !t->complete || t->cancelled || t->totalBytes != i.size ||
                t->sourceDigest != i.sha256 || !std::filesystem::is_regular_file(t->finalPath) || std::filesystem::file_size(t->finalPath) != i.size || digestFile(t->finalPath) != i.sha256)
                return Result<>::failure(invalid(ErrorCode::AuthenticationFailed, "全部文件必须接收完成且字节与摘要一致，才能确认收到"));
        }
        SendText text{b->sender, "已人工确认收到文件交付：" + b->title, 0, {}};
        text.messageId = "delivery-confirm:" + b->id;
        text.extensions = {{"delivery", {{"version", 1}, {"action", "confirm"}, {"id", b->id}, {"digest", b->manifestDigest}}}};
        auto sent = chat_.sendText(text); if (!sent) return Result<>::failure(sent.error());
        loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); s = loaded.value(); b = bundle(s, request.bundleId);
        b->manuallyConfirmed = true; b->confirmationMessageId = sent.value(); b->state = BundleState::Confirmed;
        auto saved = save(store_, s); if (!saved) return saved; events_.push_back(*b); return Result<>::success({});
    } catch (...) { return Result<>::failure(invalid(ErrorCode::StorageFailure, "交付文件不可用，未确认收到")); }
}
std::vector<DeliveryBundle> DeliveryService::takeEvents() { auto events = std::move(events_); events_.clear(); return events; }
}
