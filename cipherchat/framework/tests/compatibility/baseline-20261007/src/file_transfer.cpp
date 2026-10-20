#include "ccfw/file_transfer.hpp"
#include "client/core/files.hpp"
#include "wire_helpers.hpp"
#include "snapshot_codec.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ccfw {
namespace {
constexpr std::uint64_t maxFileSize = 1024ULL * 1024 * 1024;
constexpr std::uint32_t metadataAck = UINT32_MAX;
std::int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Error fileError(ErrorCode code, const char* message) { return {code, "M4-02/03", message}; }
template<class T = Unit> Result<T> failure(ErrorCode code, const char* message) { return Result<T>::failure(fileError(code, message)); }
auto transfer(ClientSnapshot& snapshot, const std::string& id) {
    return std::find_if(snapshot.transfers.begin(), snapshot.transfers.end(), [&](const auto& t) { return t.fileId == id; });
}
std::uint64_t verified(const TransferCheckpoint& t) { return std::min(t.totalBytes, std::uint64_t(t.nextContiguousChunk) * cc::files::kChunkSize); }
FileProgress view(const TransferCheckpoint& t) {
    return {t.fileId, verified(t), t.totalBytes, t.complete, t.peer, t.name,
        t.outgoing || !t.complete ? t.sourceOrPartialPath : t.finalPath, t.outgoing, t.cancelled, t.path, t.timestamp};
}
Result<> save(ILocalStore& store, ClientSnapshot& snapshot) { const auto expected = snapshot.revision++; return store.commit(expected, snapshot); }
std::string safeName(const std::string& input) {
    std::string name = input;
    for (char& ch : name) if (static_cast<unsigned char>(ch) < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) ch = '_';
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty() || name == "." || name == "..") name = "download";
    // A local prefix also prevents Windows device basenames such as CON and NUL.
    return "received_" + name;
}
struct Digest {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    Digest() { if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) throw std::runtime_error("digest init"); }
    Digest(const Digest& other) : Digest() { if (EVP_MD_CTX_copy_ex(ctx.get(), other.ctx.get()) != 1) throw std::runtime_error("digest copy"); }
    void add(cc::BytesView data) { if (EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1) throw std::runtime_error("digest update"); }
    std::string value() const {
        Digest copy(*this); cc::Bytes bytes(32, '\0'); unsigned int size = 0;
        if (EVP_DigestFinal_ex(copy.ctx.get(), reinterpret_cast<unsigned char*>(bytes.data()), &size) != 1 || size != 32) throw std::runtime_error("digest final");
        return cc::base64Encode(bytes);
    }
};
struct Reader {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    Reader(const std::filesystem::path& path, bool lockSource) {
        handle = CreateFileW(path.c_str(), GENERIC_READ, lockSource ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("open source");
    }
    ~Reader() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    cc::Bytes read(std::uint64_t offset, std::size_t count) {
        LARGE_INTEGER position; position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) throw std::runtime_error("seek source");
        cc::Bytes bytes(count, '\0'); DWORD size = 0;
        if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(count), &size, nullptr) || size != count) throw std::runtime_error("read source");
        return bytes;
    }
#else
    std::ifstream stream;
    Reader(const std::filesystem::path& path, bool) : stream(path, std::ios::binary) { if (!stream) throw std::runtime_error("open source"); }
    cc::Bytes read(std::uint64_t offset, std::size_t count) {
        stream.clear(); stream.seekg(static_cast<std::streamoff>(offset)); cc::Bytes bytes(count, '\0');
        if (!stream.read(bytes.data(), static_cast<std::streamsize>(count))) throw std::runtime_error("read source"); return bytes;
    }
#endif
};
void writeBlock(const std::filesystem::path& path, std::uint64_t offset, cc::BytesView data) {
    if (std::filesystem::file_size(path) < offset) throw std::runtime_error("partial shortened");
    std::filesystem::resize_file(path, offset); // rollback a previous uncommitted tail
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("open partial");
    LARGE_INTEGER position; position.QuadPart = static_cast<LONGLONG>(offset); DWORD count = 0;
    const bool ok = SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
        WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) && count == data.size() && FlushFileBuffers(file);
    CloseHandle(file); if (!ok) throw std::runtime_error("flush partial");
#else
    const int fd = ::open(path.c_str(), O_WRONLY); if (fd < 0) throw std::runtime_error("open partial");
    const bool ok = ::pwrite(fd, data.data(), data.size(), static_cast<off_t>(offset)) == static_cast<ssize_t>(data.size()) && ::fsync(fd) == 0;
    ::close(fd); if (!ok) throw std::runtime_error("flush partial");
#endif
}
struct Chunk {
    std::string id;
    std::uint32_t seq = 0;
    cc::Bytes cipher, tag;
};
// A file block is a flat, five-field CBOR map. Reject containers at their
// first SAX event, before an untrusted payload can build a recursive DOM.
struct ChunkParser final : nlohmann::json_sax<nlohmann::json> {
    Chunk chunk;
    unsigned seen = 0, field = 0;
    bool started = false, complete = false;
    bool null() override { return false; }
    bool boolean(bool) override { return false; }
    bool number_integer(number_integer_t) override { return false; }
    bool number_unsigned(number_unsigned_t value) override {
        const auto current = field; field = 0;
        if (current == 1) return value == 2;
        if (current != 4 || value > UINT32_MAX) return false;
        chunk.seq = static_cast<std::uint32_t>(value); return true;
    }
    bool number_float(number_float_t, const string_t&) override { return false; }
    bool string(string_t& value) override {
        if (field != 2 || value.empty() || value.size() > 32) return false;
        chunk.id = std::move(value); field = 0; return true;
    }
    bool binary(binary_t& value) override {
        if ((field == 8 && value.size() <= cc::files::kChunkSize) || (field == 16 && value.size() == 16)) {
            auto& target = field == 8 ? chunk.cipher : chunk.tag;
            target.assign(value.begin(), value.end()); field = 0; return true;
        }
        return false;
    }
    bool start_object(std::size_t count) override {
        if (started || count != 5) return false;
        started = true; return true;
    }
    bool key(string_t& value) override {
        if (!started || complete || field) return false;
        const unsigned next = value == "version" ? 1 : value == "file_id" ? 2 : value == "seq" ? 4 :
            value == "cipher" ? 8 : value == "tag" ? 16 : 0;
        if (!next || (seen & next)) return false;
        field = next; seen |= next; return true;
    }
    bool end_object() override { complete = seen == 31 && field == 0; return complete; }
    bool start_array(std::size_t) override { return false; }
    bool end_array() override { return false; }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
};
Chunk decodeChunk(const WireEnvelope& envelope) {
    wire::require(envelope.kind == "file_chunk" && envelope.groupId == 0 && envelope.payloadBase64.size() <= 128 * 1024);
    cc::Bytes bytes; wire::require(cc::base64Decode(envelope.payloadBase64, bytes));
    wire::require(bytes.starts_with("CCF3"));
    ChunkParser parser;
    wire::require(nlohmann::json::sax_parse(bytes.begin() + 4, bytes.end(), &parser, nlohmann::json::input_format_t::cbor) && parser.complete);
    auto chunk = std::move(parser.chunk);
    cc::Bytes id; wire::require(cc::base64Decode(chunk.id, id) && id.size() == 16 && chunk.tag.size() == 16);
    wire::require(envelope.messageId == "fchunk:" + chunk.id + ":" + std::to_string(chunk.seq));
    return chunk;
}
}

struct FileTransferService::Impl {
    struct Runtime {
        std::unique_ptr<Reader> reader;
        std::unique_ptr<Digest> hash = std::make_unique<Digest>();
        std::uint64_t offset = 0, limit = 0;
        bool ready = false, failed = false;
        std::int64_t lastSend = 0;
        std::int64_t bufferedSince = 0, retryDelay = 3000;
        std::int64_t directDeadline = 0;
        std::map<std::uint32_t, std::int64_t> sentAt;
    };
    std::filesystem::path downloads;
    std::map<std::string, Runtime> runtimes;
    std::map<std::string, WireEnvelope> deferred;
    std::map<std::string, std::map<std::uint32_t, WireEnvelope>> reordered;
    std::vector<FileProgress> events;
};
FileTransferService::FileTransferService(ITransport& net, ILocalStore& store, ISessionService& sessions, DirectFileChannel* direct)
    : impl_(std::make_unique<Impl>()), net_(net), store_(store), sessions_(sessions), direct_(direct) {}
FileTransferService::~FileTransferService() = default;
void FileTransferService::setDirectEnabled(bool enabled) {
    directEnabled_ = enabled;
    if (!enabled && direct_) direct_->close();
}
Result<> FileTransferService::setDownloadDirectory(const std::filesystem::path& directory) {
    try {
        if (directory.empty()) return failure(ErrorCode::InvalidArgument, "请选择下载目录");
        std::filesystem::create_directories(directory); impl_->downloads = std::filesystem::canonical(directory);
        return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::StorageFailure, "下载目录不可用"); }
}
Result<std::string> FileTransferService::start(const std::string& peer, const std::filesystem::path& source) {
    auto loaded = store_.load(); if (!loaded) return Result<std::string>::failure(loaded.error()); auto snapshot = loaded.value();
    if (!snapshot.identity || peer.empty() || peer.size() > 32 || peer == snapshot.account.username)
        return failure<std::string>(ErrorCode::InvalidArgument, "文件收件人或本地身份无效");
    if (std::count_if(snapshot.transfers.begin(), snapshot.transfers.end(), [](const auto& t) { return t.transportVersion == 3 && !t.complete && !t.cancelled; }) >= 16)
        return failure<std::string>(ErrorCode::Forbidden, "活动文件传输已达上限");
    try {
        if (!std::filesystem::is_regular_file(source)) return failure<std::string>(ErrorCode::InvalidArgument, "请选择普通文件");
        TransferCheckpoint checkpoint;
        checkpoint.fileId = wire::id(); checkpoint.peer = peer; checkpoint.sourceOrPartialPath = std::filesystem::canonical(source);
        checkpoint.totalBytes = std::filesystem::file_size(source); checkpoint.outgoing = true; checkpoint.timestamp = wire::now();
        checkpoint.name = codec::pathText(source.filename());
        if (checkpoint.totalBytes > maxFileSize || checkpoint.name.size() > 200) return failure<std::string>(ErrorCode::InvalidArgument, "文件超过 1 GiB 或文件名过长");
        checkpoint.chunkCount = static_cast<std::uint32_t>((checkpoint.totalBytes + 65535) / 65536);
        checkpoint.fileKey = cc::crypto::randomBytes(32);
        Impl::Runtime runtime; runtime.reader = std::make_unique<Reader>(checkpoint.sourceOrPartialPath, true); runtime.limit = checkpoint.totalBytes;
        snapshot.transfers.push_back(checkpoint); auto committed = save(store_, snapshot);
        if (!committed) return Result<std::string>::failure(committed.error());
        impl_->runtimes.emplace(checkpoint.fileId, std::move(runtime)); impl_->events.push_back(view(checkpoint));
        return Result<std::string>::success(checkpoint.fileId);
    } catch (const std::exception&) { return failure<std::string>(ErrorCode::StorageFailure, "源文件无法打开或正在被写入"); }
}
Result<> FileTransferService::acceptMetadata(const PlainEnvelope& plain) {
    try {
        wire::require(plain.kind == "file_meta" && plain.groupId == 0);
        const auto body = nlohmann::json::parse(plain.plaintext); wire::require(wire::integer(body, "version", 2) == 2);
        TransferCheckpoint checkpoint;
        checkpoint.fileId = wire::text(body, "file_id", 32); cc::Bytes id;
        wire::require(cc::base64Decode(checkpoint.fileId, id) && id.size() == 16);
        wire::require(plain.messageId == "fmeta:" + checkpoint.fileId);
        checkpoint.peer = plain.from; checkpoint.name = wire::text(body, "name", 200);
        checkpoint.timestamp = std::max<std::int64_t>(0, plain.timestamp);
        checkpoint.totalBytes = static_cast<std::uint64_t>(wire::integer(body, "size_bytes", maxFileSize));
        wire::require(wire::integer(body, "chunk_size", 65536) == 65536);
        checkpoint.chunkCount = static_cast<std::uint32_t>(wire::integer(body, "chunk_count", 16384));
        if (body.value("file_transport", std::string{}) != "window-cbor-v1" || wire::integer(body, "window", 16) != 16)
            return failure(ErrorCode::ProtocolError, "旧文件传输已移除，请双方更新客户端后重新发送");
        wire::require(checkpoint.chunkCount == (checkpoint.totalBytes + 65535) / 65536);
        checkpoint.fileKey = wire::bytes(body, "key_base64", 32); wire::require(checkpoint.fileKey.size() == 32);
        checkpoint.sourceDigest = wire::text(body, "sha256", 44); cc::Bytes hash;
        wire::require(cc::base64Decode(checkpoint.sourceDigest, hash) && hash.size() == 32);
        auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value();
        if (plain.to != snapshot.account.username) return failure(ErrorCode::ProtocolError, "文件收件人不匹配");
        const auto existing = transfer(snapshot, checkpoint.fileId);
        if (existing != snapshot.transfers.end()) {
            if (existing->transportVersion != 3 || existing->outgoing || existing->peer != checkpoint.peer || existing->sourceDigest != checkpoint.sourceDigest ||
                existing->fileKey != checkpoint.fileKey || existing->totalBytes != checkpoint.totalBytes || existing->name != checkpoint.name)
                return failure(ErrorCode::Conflict, "文件标识已绑定其他传输");
            return Result<>::success({});
        }
        if (impl_->downloads.empty()) return failure(ErrorCode::InvalidState, "请先设置下载目录");
        if (std::count_if(snapshot.transfers.begin(), snapshot.transfers.end(), [](const auto& t) { return t.transportVersion == 3 && !t.complete && !t.cancelled; }) >= 16)
            return failure(ErrorCode::Forbidden, "活动文件传输已达上限");
        const auto suffix = cc::crypto::sha256(checkpoint.fileId); static constexpr char hex[] = "0123456789abcdef";
        std::string token; for (unsigned char ch : suffix.substr(0, 8)) { token += hex[ch >> 4]; token += hex[ch & 15]; }
        checkpoint.finalPath = impl_->downloads / codec::path(token + "_" + safeName(checkpoint.name));
        checkpoint.sourceOrPartialPath = checkpoint.finalPath; checkpoint.sourceOrPartialPath += ".part";
        if (body.contains("direct") && body.at("direct").is_object()) {
            DirectFileEndpoint endpoint;
            const auto& direct = body.at("direct"); wire::require(direct.is_object());
            endpoint.fileId = wire::text(direct, "file_id", 32);
            const auto& hosts = body.at("direct").at("hosts"); wire::require(hosts.is_array() && hosts.size() > 0 && hosts.size() <= 8);
            for (const auto& host : hosts) { wire::require(host.is_string() && host.get<std::string>().size() <= 128); endpoint.hosts.push_back(host.get<std::string>()); }
            endpoint.port = static_cast<std::uint16_t>(wire::integer(direct, "port", 65535));
            endpoint.token = wire::text(direct, "token", 128);
            wire::require(endpoint.fileId == checkpoint.fileId && endpoint.port > 0 && endpoint.token.size() >= 16);
            checkpoint.directEndpoint = endpoint; checkpoint.path = 1;
            // One active direct file per client; additional transfers use Relay.
            const bool occupied = direct_ && std::any_of(snapshot.transfers.begin(), snapshot.transfers.end(), [&](const auto& t) {
                return t.fileId == direct_->fileId() && !t.complete && !t.cancelled;
            });
            if (directEnabled_ && direct_ && !occupied) {
                if (!direct_->connect(endpoint)) checkpoint.path = 3;
            } else checkpoint.path = 3;
        }
        if (std::filesystem::exists(checkpoint.finalPath) || std::filesystem::exists(checkpoint.sourceOrPartialPath))
            return failure(ErrorCode::Conflict, "下载目标已存在，原文件已保留");
        if (checkpoint.totalBytes == 0) wire::require(checkpoint.sourceDigest == cc::base64Encode(cc::crypto::sha256("")));
        // Reserve the trusted local path before creating it, so a crash can recreate an empty partial.
        snapshot.transfers.push_back(checkpoint); auto committed = save(store_, snapshot); if (!committed) return committed;
        std::ofstream part(checkpoint.sourceOrPartialPath, std::ios::binary); if (!part || !part.flush()) throw std::runtime_error("create partial"); part.close();
        Impl::Runtime runtime; runtime.ready = true; runtime.directDeadline = monotonicMs() + 3000; impl_->runtimes.emplace(checkpoint.fileId, std::move(runtime));
        impl_->events.push_back(view(checkpoint)); return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::ProtocolError, "文件元数据格式、尺寸或摘要无效"); }
}
Result<> FileTransferService::consume(const PlainEnvelope& plain) {
    Result<> result = Result<>::success({});
    try {
        if (plain.kind == "file_meta") result = acceptMetadata(plain);
        else if (plain.kind == "file_ack") {
            const auto body = nlohmann::json::parse(plain.plaintext);
            wire::require(plain.groupId == 0 && wire::integer(body, "version", 2) == 2);
            if (body.value("file_transport", std::string{}) != "window-cbor-v1")
                return failure(ErrorCode::ProtocolError, "旧文件传输已移除，请对端更新客户端");
            const auto window = static_cast<std::uint32_t>(wire::integer(body, "window", 16));
            result = acknowledge(plain.from, wire::text(body, "file_id", 32), static_cast<std::uint32_t>(wire::integer(body, "seq", UINT32_MAX)), window);
        } else return failure(ErrorCode::ProtocolError, "未知文件会话载荷");
        if (!result) return result;
        auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value();
        std::erase_if(snapshot.pendingPayloads, [&](const auto& p) { return p.messageId == plain.messageId; });
        auto committed = save(store_, snapshot); if (!committed) return committed;
        (void)net_.send(cc::proto::makeRecipientAck(plain.messageId, plain.from));
        return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::ProtocolError, "文件确认格式无效"); }
}
Result<> FileTransferService::receiveMetadata(const WireEnvelope& envelope) {
    if (envelope.kind != "file_meta") return failure(ErrorCode::ProtocolError, "文件元数据路由错误");
    auto decrypted = sessions_.decryptAndCommit(envelope);
    if (decrypted) return consume(decrypted.value());
    if (decrypted.error().code == ErrorCode::AlreadyProcessed) {
        auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
        for (const auto& plain : loaded.value().pendingPayloads) if (plain.messageId == envelope.messageId) return consume(plain);
        (void)net_.send(cc::proto::makeRecipientAck(envelope.messageId, envelope.from));
        return Result<>::success({});
    }
    if (decrypted.error().code == ErrorCode::IdentityChanged || decrypted.error().code == ErrorCode::StorageFailure) {
        if (impl_->deferred.size() >= 64 && !impl_->deferred.count(envelope.messageId)) return failure(ErrorCode::Forbidden, "待接收文件元数据已满");
        const bool first = !impl_->deferred.count(envelope.messageId); impl_->deferred[envelope.messageId] = envelope;
        if (first && decrypted.error().code == ErrorCode::IdentityChanged) (void)net_.send(cc::proto::withV2(cc::proto::makeFetchBundle(envelope.from)));
    }
    return Result<>::failure(decrypted.error());
}
Result<> FileTransferService::receiveAck(const WireEnvelope& envelope) {
    if (envelope.kind != "file_ack") return failure(ErrorCode::ProtocolError, "文件确认路由错误");
    auto decrypted = sessions_.decryptAndCommit(envelope);
    if (decrypted) return consume(decrypted.value());
    if (decrypted.error().code == ErrorCode::AlreadyProcessed) {
        auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
        for (const auto& plain : loaded.value().pendingPayloads) if (plain.messageId == envelope.messageId) return consume(plain);
        (void)net_.send(cc::proto::makeRecipientAck(envelope.messageId, envelope.from)); return Result<>::success({});
    }
    return Result<>::failure(decrypted.error());
}
Result<> FileTransferService::sendAck(const std::string& id) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value();
    auto t = transfer(snapshot, id); if (t == snapshot.transfers.end() || t->outgoing || t->cancelled || t->transportVersion != 3) return failure(ErrorCode::InvalidState, "文件签收状态不可用");
    if (t->nextContiguousChunk == t->chunkCount && !t->complete) return Result<>::success({});
    const auto sequence = t->nextContiguousChunk == 0 ? metadataAck : t->nextContiguousChunk - 1;
    const auto messageId = "fack:" + id + ":" + std::to_string(sequence);
    std::optional<WireEnvelope> wireAck;
    if (t->lastAck && t->lastAck->messageId == messageId) wireAck = t->lastAck;
    if (!wireAck) for (const auto& out : snapshot.outbox) if (out.messageId == messageId) wireAck = out;
    if (!wireAck) {
        nlohmann::json body = {{"version", 2}, {"file_id", id}, {"seq", sequence}};
        body["file_transport"] = "window-cbor-v1"; body["window"] = 16;
        auto encrypted = sessions_.encryptAndCommit({messageId, "", snapshot.account.username, t->peer, "file_ack", body.dump(), 0, wire::now()});
        if (!encrypted) return Result<>::failure(encrypted.error());
        wireAck = encrypted.value();
    }
    if (!t->lastAck || t->lastAck->messageId != messageId) {
        loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); snapshot = loaded.value(); t = transfer(snapshot, id);
        t->lastAck = wireAck; auto committed = save(store_, snapshot); if (!committed) return committed;
    }
    if (directEnabled_ && direct_ && direct_->fileId() == id && t->path == 2 && direct_->connected()) {
        auto sent = direct_->send(*wireAck); if (sent) return Result<>::success({});
        t->path = 3;
        auto pathSaved = save(store_, snapshot); if (!pathSaved) return pathSaved;
    }
    (void)net_.send(wire::send(*wireAck)); return Result<>::success({});
}
Result<> FileTransferService::acknowledge(const std::string& peer, const std::string& id, std::uint32_t sequence, std::uint32_t window) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value(); auto t = transfer(snapshot, id);
    if (t == snapshot.transfers.end() || !t->outgoing || t->peer != peer) return failure(ErrorCode::ProtocolError, "文件签收对端或标识不匹配");
    if (t->cancelled) return failure(ErrorCode::Cancelled, "文件传输已取消");
    if (t->transportVersion != 3 || window != 16) return failure(ErrorCode::ProtocolError, "旧文件传输已移除，请对端更新客户端");
    if (sequence == metadataAck) {
        if (!t->metadataQueued) return failure(ErrorCode::ProtocolError, "元数据尚未发送");
        if (t->metadataAcknowledged) return Result<>::success({});
        t->metadataAcknowledged = true; if (!t->chunkCount) t->complete = true;
    } else {
        if (sequence >= t->chunkCount) return failure(ErrorCode::ProtocolError, "文件签收序号越界");
        if (sequence < t->nextContiguousChunk) return Result<>::success({});
        const auto count = sequence - t->nextContiguousChunk + 1;
        if (count > t->pendingChunks.size()) return failure(ErrorCode::ProtocolError, "累计签收超过已发送窗口");
        t->previousTag = t->pendingChunks[count - 1].tag;
        t->pendingChunks.erase(t->pendingChunks.begin(), t->pendingChunks.begin() + count);
        t->nextContiguousChunk = sequence + 1;
        t->complete = t->nextContiguousChunk == t->chunkCount;
    }
    const auto event = view(*t); auto committed = save(store_, snapshot); if (!committed) return committed;
    if (auto runtime = impl_->runtimes.find(id); runtime != impl_->runtimes.end()) {
        std::erase_if(runtime->second.sentAt, [&](const auto& item) { return item.first < t->nextContiguousChunk; });
        runtime->second.retryDelay = 3000;
    }
    if (event.complete) {
        impl_->runtimes.erase(id);
        if (direct_ && direct_->fileId() == id) direct_->close();
    }
    impl_->events.push_back(event); return Result<>::success({});
}
Result<> FileTransferService::finish(const std::string& id) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value(); auto t = transfer(snapshot, id);
    if (t == snapshot.transfers.end()) return failure(ErrorCode::NotFound, "文件传输不存在");
    if (t->complete) return Result<>::success({});
    auto runtime = impl_->runtimes.find(id);
    if (t->outgoing || t->cancelled || t->nextContiguousChunk != t->chunkCount || runtime == impl_->runtimes.end() || !runtime->second.ready)
        return failure(ErrorCode::InvalidState, "文件尚未完成验证");
    try {
        if (runtime->second.hash->value() != t->sourceDigest) return failure(ErrorCode::AuthenticationFailed, "文件完整摘要不匹配");
        runtime->second.reader.reset();
        if (std::filesystem::exists(t->sourceOrPartialPath)) {
            if (std::filesystem::file_size(t->sourceOrPartialPath) != t->totalBytes || std::filesystem::exists(t->finalPath)) return failure(ErrorCode::Conflict, "最终路径冲突或文件长度不匹配");
            writeBlock(t->sourceOrPartialPath, t->totalBytes, {});
#ifdef _WIN32
            if (!MoveFileExW(t->sourceOrPartialPath.c_str(), t->finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("rename partial");
#else
            std::filesystem::rename(t->sourceOrPartialPath, t->finalPath);
#endif
        } else if (!std::filesystem::exists(t->finalPath) || std::filesystem::file_size(t->finalPath) != t->totalBytes) throw std::runtime_error("missing final");
        t->complete = true; const auto event = view(*t); auto committed = save(store_, snapshot); if (!committed) return committed;
        impl_->runtimes.erase(id); impl_->events.push_back(event); return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::StorageFailure, "文件最终落盘失败，临时文件已保留"); }
}
Result<> FileTransferService::receiveChunk(const WireEnvelope& envelope) {
    try {
        const auto chunk = decodeChunk(envelope);
        auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
        const auto& snapshot = loaded.value();
        auto t = std::find_if(snapshot.transfers.begin(), snapshot.transfers.end(), [&](const auto& entry) { return entry.fileId == chunk.id; });
        if (t == snapshot.transfers.end() || t->outgoing || t->peer != envelope.from || snapshot.account.username != envelope.to || chunk.seq >= t->chunkCount)
            return failure(ErrorCode::ProtocolError, "文件块对端、标识或序号不匹配");
        if (t->cancelled) return failure(ErrorCode::Cancelled, "文件传输已取消");
        if (t->transportVersion != 3) return failure(ErrorCode::ProtocolError, "旧文件需重新发送");
        if (chunk.seq >= t->nextContiguousChunk) {
            if (chunk.seq - t->nextContiguousChunk >= 64) return failure(ErrorCode::Forbidden, "文件块超过接收窗口");
            std::size_t count = 0; for (const auto& [_, queue] : impl_->reordered) count += queue.size();
            auto& queue = impl_->reordered[chunk.id];
            if (const auto prior = queue.find(chunk.seq); prior != queue.end()) {
                return prior->second.payloadBase64 == envelope.payloadBase64 ? Result<>::success({}) : failure(ErrorCode::Conflict, "同序号文件块冲突");
            }
            if (count >= 64) return failure(ErrorCode::Forbidden, "文件接收缓存已满");
            queue.emplace(chunk.seq, envelope);
            if (auto runtime = impl_->runtimes.find(chunk.id); runtime != impl_->runtimes.end() && !runtime->second.bufferedSince)
                runtime->second.bufferedSince = monotonicMs();
            return Result<>::success({});
        }
        const auto digest = cc::base64Encode(cc::crypto::sha256(envelope.payloadBase64));
        const auto prior = std::find_if(snapshot.receivedMessages.begin(), snapshot.receivedMessages.end(), [&](const auto& record) {
            return record.messageId == envelope.messageId && record.from == envelope.from && record.to == envelope.to &&
                record.kind == envelope.kind && record.groupId == 0 && record.ciphertextSha256 == digest;
        });
        if (prior == snapshot.receivedMessages.end()) return failure(ErrorCode::AuthenticationFailed, "重复文件块与认证记录不符");
        auto ack = sendAck(chunk.id); if (!ack) return ack;
        (void)net_.send(cc::proto::makeRecipientAck(envelope.messageId, envelope.from));
        return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::ProtocolError, "文件块格式无效"); }
}
Result<> FileTransferService::processWindow(const std::string& id, bool force, std::size_t budget) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value();
    auto t = transfer(snapshot, id); auto runtime = impl_->runtimes.find(id);
    if (t == snapshot.transfers.end() || t->outgoing || t->transportVersion != 3 || t->cancelled)
        return failure(ErrorCode::InvalidState, "接收窗口不可用");
    if (runtime == impl_->runtimes.end() || !runtime->second.ready) return Result<>::success({});
    auto& queue = impl_->reordered[id];
    std::size_t count = 0;
    while (count < std::min<std::size_t>(4, budget) && queue.count(t->nextContiguousChunk + static_cast<std::uint32_t>(count))) ++count;
    if (!count || (!force && count < 4 && t->nextContiguousChunk + count < t->chunkCount)) return Result<>::success({});
    try {
        const auto first = t->nextContiguousChunk;
        auto candidateHash = std::make_unique<Digest>(*runtime->second.hash);
        cc::Bytes rawId, batch; wire::require(cc::base64Decode(id, rawId));
        std::vector<WireEnvelope> committedPackets;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& envelope = queue.at(t->nextContiguousChunk);
            const auto chunk = decodeChunk(envelope);
            const auto plain = cc::files::openChunk(t->fileKey, rawId, chunk.seq, t->previousTag, chunk.cipher, chunk.tag);
            if (!plain || plain->size() != std::min<std::uint64_t>(65536, t->totalBytes - verified(*t))) {
                queue.erase(chunk.seq);
                return failure(ErrorCode::AuthenticationFailed, "文件窗口块认证失败");
            }
            candidateHash->add(*plain); batch += *plain;
            ++t->nextContiguousChunk; t->previousTag = chunk.tag;
            snapshot.receivedMessages.push_back({envelope.messageId, envelope.from, envelope.to, envelope.kind, 0,
                cc::base64Encode(cc::crypto::sha256(envelope.payloadBase64))});
            if (snapshot.receivedMessages.size() > 4096) snapshot.receivedMessages.erase(snapshot.receivedMessages.begin());
            committedPackets.push_back(envelope);
        }
        const bool last = t->nextContiguousChunk == t->chunkCount;
        t->partialDigest = candidateHash->value();
        if (last && t->partialDigest != t->sourceDigest) return failure(ErrorCode::AuthenticationFailed, "文件完整摘要不匹配");
        writeBlock(t->sourceOrPartialPath, std::uint64_t(first) * 65536, batch);
        const auto event = view(*t); auto saved = save(store_, snapshot); if (!saved) return saved;
        runtime->second.hash = std::move(candidateHash);
        for (std::uint32_t sequence = first; sequence < first + count; ++sequence) queue.erase(sequence);
        runtime->second.bufferedSince = queue.empty() ? 0 : monotonicMs();
        impl_->events.push_back(event);
        if (last) { auto finalized = finish(id); if (!finalized) return finalized; }
        auto acked = sendAck(id); if (!acked) return acked;
        for (const auto& envelope : committedPackets) (void)net_.send(cc::proto::makeRecipientAck(envelope.messageId, envelope.from));
        return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::StorageFailure, "文件窗口写入失败，已保留检查点"); }
}
Result<> FileTransferService::resume(const std::string& id) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value(); auto t = transfer(snapshot, id);
    if (t == snapshot.transfers.end()) return failure(ErrorCode::NotFound, "文件传输不存在");
    if (t->cancelled) return failure(ErrorCode::Cancelled, "已取消传输不能复用旧文件密钥，请重新发送");
    if (t->complete) return Result<>::success({});
    if (t->transportVersion != 3) return failure(ErrorCode::ProtocolError, "旧文件传输已移除，请重新发送原文件");
    try {
        if (t->path == 1 || t->path == 2) {
            t->path = 3; t->directEndpoint.reset();
            auto committed = save(store_, snapshot); if (!committed) return committed;
            if (direct_ && direct_->fileId() == id) direct_->close();
            impl_->events.push_back(view(*t));
        }
        impl_->runtimes.erase(id); Impl::Runtime runtime;
        auto path = t->sourceOrPartialPath;
        if (t->outgoing) {
            runtime.reader = std::make_unique<Reader>(path, true); runtime.limit = t->totalBytes;
            if (std::filesystem::file_size(path) != t->totalBytes) return failure(ErrorCode::Conflict, "源文件大小变化，拒绝复用旧密钥");
        } else {
            runtime.limit = verified(*t);
            if (!std::filesystem::exists(path) && t->nextContiguousChunk == t->chunkCount && std::filesystem::exists(t->finalPath)) path = t->finalPath;
            if (!std::filesystem::exists(path) && runtime.limit == 0) {
                std::ofstream empty(path, std::ios::binary); if (!empty) throw std::runtime_error("create partial");
            }
            if (std::filesystem::file_size(path) < runtime.limit) return failure(ErrorCode::StorageFailure, "临时文件短于已验证进度");
            if (std::filesystem::file_size(path) > runtime.limit) std::filesystem::resize_file(path, runtime.limit);
            runtime.reader = std::make_unique<Reader>(path, false);
        }
        impl_->runtimes.emplace(id, std::move(runtime)); return Result<>::success({});
    } catch (const std::exception&) { return failure(ErrorCode::StorageFailure, "文件续传源或临时文件不可用"); }
}
Result<> FileTransferService::cancel(const std::string& id) {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value(); auto t = transfer(snapshot, id);
    if (t == snapshot.transfers.end()) return failure(ErrorCode::NotFound, "文件传输不存在");
    if (t->complete || t->cancelled) return Result<>::success({});
    t->cancelled = true;
    std::erase_if(snapshot.pendingSends, [&](const auto& p) { return p.messageId == "fmeta:" + id; });
    std::erase_if(snapshot.outbox, [&](const auto& p) { return p.messageId == "fmeta:" + id; });
    const auto event = view(*t); auto committed = save(store_, snapshot); if (!committed) return committed;
    if (direct_ && direct_->fileId() == id) direct_->close();
    impl_->runtimes.erase(id); impl_->reordered.erase(id); impl_->events.push_back(event); return Result<>::success({});
}
Result<FileProgress> FileTransferService::progress(const std::string& id) const {
    auto loaded = store_.load(); if (!loaded) return Result<FileProgress>::failure(loaded.error());
    auto t = transfer(loaded.value(), id); if (t == loaded.value().transfers.end()) return failure<FileProgress>(ErrorCode::NotFound, "文件传输不存在");
    return Result<FileProgress>::success(view(*t));
}
std::vector<FileProgress> FileTransferService::takeEvents() { auto result = std::move(impl_->events); impl_->events.clear(); return result; }
Result<> FileTransferService::tick() {
    auto loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    int budget = 4;
    if (direct_) {
        for (int i = 0; i < 16; ++i) {
            auto incoming = direct_->poll(); if (!incoming) return Result<>::failure(incoming.error());
            if (!incoming.value()) break;
            const auto& envelope = *incoming.value();
            Result<> handled = envelope.kind == "file_chunk" ? receiveChunk(envelope) : envelope.kind == "file_ack" ? receiveAck(envelope) : failure(ErrorCode::ProtocolError, "直连载荷类型无效");
            if (!handled) return handled;
        }
    }
    const auto deferred = impl_->deferred;
    for (const auto& [id, envelope] : deferred) {
        --budget;
        auto result = receiveMetadata(envelope);
        if (result) impl_->deferred.erase(id);
        else if (result.error().code != ErrorCode::IdentityChanged) return result;
        break; // A pending trust prompt must not starve unrelated active files.
    }
    loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error());
    for (const auto& plain : loaded.value().pendingPayloads) {
        // Keep old records readable, but do not let a retired protocol's inbox
        // block unrelated transfers after an upgrade or a reconnect.
        if (!wire::currentFileMessage(loaded.value(), plain.kind, plain.messageId)) continue;
        if (plain.kind == "file_meta" || plain.kind == "file_ack") {
            const auto body = nlohmann::json::parse(plain.plaintext, nullptr, false);
            if (body.is_object() && (!body.contains("file_transport") || body.at("file_transport") != "window-cbor-v1")) continue;
        }
        if (budget-- <= 0) break;
        auto result = consume(plain); if (!result) return result;
        break;
    }
    const auto transferList = loaded.value().transfers;
    for (const auto& initial : transferList) {
        if (budget <= 0) break;
        if (initial.cancelled || initial.transportVersion != 3) continue;
        if (initial.complete) {
            if (!initial.outgoing) {
                const auto seq = initial.nextContiguousChunk == 0 ? metadataAck : initial.nextContiguousChunk - 1;
                if (!initial.lastAck || initial.lastAck->messageId != "fack:" + initial.fileId + ":" + std::to_string(seq)) {
                    auto ack = sendAck(initial.fileId); if (!ack) return ack; --budget;
                }
            }
            continue;
        }
        if (!impl_->runtimes.count(initial.fileId)) { auto resumed = resume(initial.fileId); if (!resumed) return resumed; }
        auto& runtime = impl_->runtimes.at(initial.fileId); if (runtime.failed) continue;
        try {
            if (!runtime.ready) {
                while (budget > 0 && runtime.offset < runtime.limit) {
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(65536, runtime.limit - runtime.offset));
                    const auto bytes = runtime.reader->read(runtime.offset, count); runtime.hash->add(bytes); runtime.offset += count; --budget;
                }
                if (runtime.offset != runtime.limit) continue;
                if (initial.outgoing && !initial.sourceDigest.empty() && runtime.hash->value() != initial.sourceDigest) {
                    runtime.failed = true; runtime.reader.reset(); return failure(ErrorCode::Conflict, "源文件摘要变化，拒绝复用旧文件密钥");
                }
                if (!initial.outgoing && !initial.partialDigest.empty() && runtime.hash->value() != initial.partialDigest) {
                    runtime.failed = true; runtime.reader.reset(); return failure(ErrorCode::AuthenticationFailed, "临时文件已确认前缀被修改");
                }
                runtime.ready = true; if (!initial.outgoing) runtime.reader.reset();
            }
            loaded = store_.load(); if (!loaded) return Result<>::failure(loaded.error()); auto snapshot = loaded.value(); auto t = transfer(snapshot, initial.fileId);
            if (t->path == 1 || t->path == 2) {
                const auto previousPath = t->path;
                const bool bound = directEnabled_ && direct_ && direct_->fileId() == t->fileId;
                if (!bound || direct_->failed()) t->path = 3;
                else if (direct_->connected()) t->path = 2;
                else if (t->path == 2) t->path = 3;
                else if (!t->outgoing || t->metadataAcknowledged) {
                    if (!runtime.directDeadline) runtime.directDeadline = monotonicMs() + 3000;
                    if (monotonicMs() >= runtime.directDeadline) t->path = 3;
                }
                if (previousPath != t->path) {
                    if (t->path == 3) {
                        if (bound) direct_->close();
                        runtime.sentAt.clear(); runtime.retryDelay = 3000;
                    }
                    auto saved = save(store_, snapshot); if (!saved) return saved;
                    impl_->events.push_back(view(*t));
                }
            }
            if (!t->outgoing) {
                if (t->nextContiguousChunk == t->chunkCount) {
                    auto finalized = finish(t->fileId); if (!finalized) return finalized;
                    auto ack = sendAck(initial.fileId); if (!ack) return ack;
                } else {
                    if (!runtime.bufferedSince && !impl_->reordered[t->fileId].empty()) runtime.bufferedSince = monotonicMs();
                    const bool force = runtime.bufferedSince && monotonicMs() - runtime.bufferedSince >= 100;
                    const bool resendAck = wire::now() - runtime.lastSend >= 3000;
                    const auto beforeEvents = impl_->events.size();
                    auto received = processWindow(t->fileId, force, budget); if (!received) return received;
                    if (impl_->events.size() != beforeEvents) budget = 0;
                    if (resendAck) {
                        auto ack = sendAck(initial.fileId); if (!ack) return ack;
                        if (auto current = impl_->runtimes.find(initial.fileId); current != impl_->runtimes.end()) current->second.lastSend = wire::now();
                    }
                }
                continue;
            }
            if (!t->metadataQueued) {
                // Do not listen at application startup or while hashing large sources.
                const bool occupied = direct_ && std::any_of(snapshot.transfers.begin(), snapshot.transfers.end(), [&](const auto& other) {
                    return other.fileId == direct_->fileId() && !other.complete && !other.cancelled;
                });
                if (directEnabled_ && direct_ && !occupied) {
                    auto endpoint = direct_->listen(t->fileId, cc::base64Encode(cc::crypto::randomBytes(24)));
                    if (endpoint) { t->directEndpoint = endpoint.value(); t->path = 1; }
                }
                t->sourceDigest = runtime.hash->value(); t->metadataQueued = true;
                nlohmann::json meta = {{"version", 2}, {"file_id", t->fileId}, {"name", t->name}, {"size_bytes", t->totalBytes},
                    {"chunk_size", 65536}, {"chunk_count", t->chunkCount}, {"key_base64", cc::base64Encode(t->fileKey)}, {"sha256", t->sourceDigest}};
                meta["file_transport"] = "window-cbor-v1"; meta["window"] = 16;
                if (t->directEndpoint) meta["direct"] = { {"file_id", t->directEndpoint->fileId}, {"hosts", t->directEndpoint->hosts}, {"port", t->directEndpoint->port}, {"token", t->directEndpoint->token} };
                if (std::count_if(snapshot.pendingSends.begin(), snapshot.pendingSends.end(), [&](const auto& p) { return p.to == t->peer; }) >= 64)
                    return failure(ErrorCode::Forbidden, "对端待建会话队列已满");
                snapshot.pendingSends.push_back({"fmeta:" + t->fileId, "", snapshot.account.username, t->peer, "file_meta", meta.dump(), 0, t->timestamp > 0 ? t->timestamp : wire::now()});
                auto committed = save(store_, snapshot); if (!committed) return committed;
                (void)net_.send(cc::proto::withV2(cc::proto::makeFetchBundle(t->peer)));
            }
            if (!t->metadataAcknowledged || budget <= 0) continue;
            if (t->path == 1) continue;
            {
                bool changed = false;
                while (budget > 0 && t->pendingChunks.size() < 16 && t->nextContiguousChunk + t->pendingChunks.size() < t->chunkCount) {
                    --budget; changed = true;
                    const auto sequence = t->nextContiguousChunk + static_cast<std::uint32_t>(t->pendingChunks.size());
                    const auto offset = std::uint64_t(sequence) * 65536;
                    const auto bytes = runtime.reader->read(offset, static_cast<std::size_t>(std::min<std::uint64_t>(65536, t->totalBytes - offset)));
                    cc::Bytes rawId; wire::require(cc::base64Decode(t->fileId, rawId));
                    const auto previous = t->pendingChunks.empty() ? t->previousTag : t->pendingChunks.back().tag;
                    const auto sealed = cc::files::sealChunk(t->fileKey, rawId, sequence, previous, bytes);
                    const nlohmann::json body = {{"version", 2}, {"file_id", t->fileId}, {"seq", sequence},
                        {"cipher", codec::binary(sealed.cipher)}, {"tag", codec::binary(sealed.tag)}};
                    const auto cbor = nlohmann::json::to_cbor(body); cc::Bytes encoded("CCF3");
                    encoded.append(reinterpret_cast<const char*>(cbor.data()), cbor.size());
                    t->pendingChunks.push_back({{"fchunk:" + t->fileId + ":" + std::to_string(sequence), snapshot.account.username, t->peer,
                        "file_chunk", cc::base64Encode(encoded), 0, wire::now()}, sealed.tag});
                }
                if (changed) { auto saved = save(store_, snapshot); if (!saved) return saved; }
                int sends = 4;
                for (std::size_t i = 0; i < t->pendingChunks.size() && sends > 0; ++i) {
                    const auto sequence = t->nextContiguousChunk + static_cast<std::uint32_t>(i);
                    const auto previousSend = runtime.sentAt.find(sequence);
                    const auto now = monotonicMs();
                    const bool retry = previousSend != runtime.sentAt.end();
                    if (retry && (i != 0 || now - previousSend->second < runtime.retryDelay)) continue;
                    Result<> sent = Result<>::success({});
                    if (t->path == 2 && directEnabled_ && direct_ && direct_->fileId() == t->fileId && direct_->connected()) sent = direct_->send(t->pendingChunks[i].envelope);
                    if (!sent || t->path != 2 || !direct_ || !direct_->connected()) {
                        if (t->path == 2) {
                            t->path = 3; runtime.sentAt.clear();
                            if (direct_ && direct_->fileId() == t->fileId) direct_->close();
                            auto pathSaved = save(store_, snapshot); if (!pathSaved) return pathSaved;
                            impl_->events.push_back(view(*t));
                        }
                        sent = net_.send(wire::send(t->pendingChunks[i].envelope));
                    }
                    if (!sent) return sent;
                    runtime.sentAt[sequence] = now; --sends;
                    if (retry) runtime.retryDelay = std::min<std::int64_t>(30000, runtime.retryDelay * 2);
                }
                continue;
            }
        } catch (const std::exception&) {
            runtime.failed = true; runtime.reader.reset(); return failure(ErrorCode::StorageFailure, "文件读写或恢复失败，已保留检查点");
        }
    }
    return Result<>::success({});
}
} // namespace ccfw
