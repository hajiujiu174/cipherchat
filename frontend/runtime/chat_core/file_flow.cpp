// 人工实现，由 ChatGPT 规范化。

#include "file_flow.hpp"

#include <algorithm>
#include <limits>

namespace nextalk {
namespace chat {
namespace {
const std::size_t kReceiveBuffer = 64;
const int kSendWindow = 16;
const int kSendPacketsPerTick = 4;
const int kReceiveBatch = 4;
const int64_t kFirstRetryMs = 3000;
const int64_t kMaxRetryMs = 30000;
}

const uint64_t kMaxFileBytes = uint64_t(1024) * 1024 * 1024;
const uint32_t kFileChunkSize = 65536;
const uint32_t kMetadataAck = std::numeric_limits<uint32_t>::max();

FileState::FileState()
    : totalBytes(0), nextContiguousChunk(0), outgoing(false), metadataQueued(false),
      metadataAcknowledged(false), complete(false), cancelled(false), chunkCount(0),
      transportVersion(3), path(0), timestamp(0), serverCache(false), cacheUploaded(false),
      cacheCopy(false), paused(false), conversationGroupId(0) {}

FileRuntimeState::FileRuntimeState()
    : offset(0), limit(0), ready(false), failed(false), lastSend(0), bufferedSince(0),
      retryDelay(kFirstRetryMs), cacheReady(false), cacheRequest(0), cacheFailureSince(0) {}

void FileRuntimeState::resetCacheConnection() {
    cacheReady = false;
    cacheRequest = 0;
    sentAt.clear();
}

uint32_t chunkCount(uint64_t bytes) {
    return static_cast<uint32_t>(bytes / kFileChunkSize + (bytes % kFileChunkSize != 0));
}

uint64_t verifiedBytes(const FileState& file) {
    return std::min(file.totalBytes, uint64_t(file.nextContiguousChunk) * kFileChunkSize);
}

std::size_t bytesInChunk(uint64_t size, uint32_t sequence) {
    const uint64_t offset = uint64_t(sequence) * kFileChunkSize;
    if (offset >= size) return 0;
    return static_cast<std::size_t>(std::min<uint64_t>(kFileChunkSize, size - offset));
}

std::string safeDownloadName(const std::string& original) {
    const std::string reserved = "<>:\"/\\|?*";
    std::string name = original;
    for (std::string::iterator ch = name.begin(); ch != name.end(); ++ch) {
        if (static_cast<unsigned char>(*ch) < 32 || reserved.find(*ch) != std::string::npos)
            *ch = '_';
    }
    while (!name.empty() && (name[name.size() - 1] == '.' || name[name.size() - 1] == ' '))
        name.erase(name.size() - 1);
    if (name.empty()) name = "download";
    return "received_" + name;
}

bool mayStartFile(const FileState& file, bool sending) {
    return file.transportVersion == 3 && !file.complete && !file.cancelled &&
           (!sending || !file.cacheUploaded);
}

bool sameIncomingFile(const FileState& previous, const FileState& received) {
    return previous.transportVersion == 3 && !previous.outgoing &&
           previous.peer == received.peer && previous.sourceDigest == received.sourceDigest &&
           previous.fileKey == received.fileKey && previous.totalBytes == received.totalBytes &&
           previous.name == received.name;
}

bool validChunkRoute(const FileState& file, const ccfw::WireEnvelope& packet,
                     const std::string& owner, uint32_t sequence) {
    return !file.outgoing && file.peer == packet.from && owner == packet.to &&
           sequence < file.chunkCount;
}

void discardSentPrefix(SentBlocks& sent, uint32_t nextSequence) {
    sent.erase(sent.begin(), sent.lower_bound(nextSequence));
}

// 对端累计签收只能推进原发送窗口中的块。
AckUpdate applyFileAck(FileState& file, uint32_t sequence) {
    if (file.serverCache) {
        if (sequence == kMetadataAck) {
            file.metadataAcknowledged = true;
            if (!file.chunkCount) file.complete = true;
        } else {
            if (sequence >= file.chunkCount) return AckOutsideFile;
            if (sequence + 1 == file.chunkCount) {
                file.complete = true;
                file.cacheUploaded = true;
                file.nextContiguousChunk = file.chunkCount;
                file.pendingChunks.clear();
            }
        }
        return AckApplied;
    }
    if (sequence == kMetadataAck) {
        if (!file.metadataQueued) return AckMetadataNotQueued;
        if (file.metadataAcknowledged) return AckRepeated;
        file.metadataAcknowledged = true;
        if (!file.chunkCount) file.complete = true;
        return AckApplied;
    }
    if (sequence >= file.chunkCount) return AckOutsideFile;
    if (sequence < file.nextContiguousChunk) return AckRepeated;
    const uint32_t count = sequence - file.nextContiguousChunk + 1;
    if (count > file.pendingChunks.size()) return AckOutsideWindow;
    file.previousTag = file.pendingChunks[count - 1].tag;
    file.pendingChunks.erase(file.pendingChunks.begin(), file.pendingChunks.begin() + count);
    file.nextContiguousChunk = sequence + 1;
    file.complete = file.nextContiguousChunk == file.chunkCount;
    return AckApplied;
}

// 缓存探测允许清空本地排队重试。已完成上传与接收端完成分开标记。
bool applyCachePosition(FileState& file, FileRuntimeState& runtime, bool begin, bool complete,
                        uint32_t next) {
    if (begin) {
        runtime.cacheReady = true;
        if (complete) {
            if (next != file.chunkCount) return false;
            file.nextContiguousChunk = next;
            file.pendingChunks.clear();
            file.cacheUploaded = true;
        } else if (next < file.nextContiguousChunk) {
            file.nextContiguousChunk = 0;
            file.previousTag.clear();
            file.pendingChunks.clear();
            runtime.sentAt.clear();
        }
    } else if (next > file.nextContiguousChunk) {
        const uint32_t count = next - file.nextContiguousChunk;
        if (count > file.pendingChunks.size()) return false;
        file.previousTag = file.pendingChunks[count - 1].tag;
        file.pendingChunks.erase(file.pendingChunks.begin(), file.pendingChunks.begin() + count);
        file.nextContiguousChunk = next;
        discardSentPrefix(runtime.sentAt, next);
        if (next == file.chunkCount) file.cacheUploaded = true;
    }
    return true;
}

ReceiveChunkStatus bufferChunk(ReceiveQueues& queues, const FileState& file, uint32_t sequence,
                               const ccfw::WireEnvelope& packet, FileRuntimeState* runtime, int64_t now) {
    if (sequence - file.nextContiguousChunk >= kReceiveBuffer) return ChunkTooFar;
    std::size_t count = 0;
    for (ReceiveQueues::const_iterator it = queues.begin(); it != queues.end(); ++it)
        count += it->second.size();
    BufferedBlocks& blocks = queues[file.fileId];
    const BufferedBlocks::const_iterator prior = blocks.find(sequence);
    if (prior != blocks.end())
        return prior->second.payloadBase64 == packet.payloadBase64 ? ChunkRepeated : ChunkConflict;
    if (count >= kReceiveBuffer) return ChunkBufferFull;
    blocks.insert(std::make_pair(sequence, packet));
    if (runtime && !runtime->bufferedSince) runtime->bufferedSince = now;
    return ChunkBuffered;
}

bool hasBufferedChunks(const ReceiveQueues& queues) {
    for (ReceiveQueues::const_iterator it = queues.begin(); it != queues.end(); ++it) {
        if (!it->second.empty()) return true;
    }
    return false;
}

std::size_t readyBatch(const FileState& file, const BufferedBlocks& blocks, bool force, std::size_t budget) {
    std::size_t count = 0;
    const std::size_t limit = std::min<std::size_t>(kReceiveBatch, budget);
    while (count < limit && blocks.count(file.nextContiguousChunk + static_cast<uint32_t>(count)))
        ++count;
    if (!force && count < kReceiveBatch && file.nextContiguousChunk + count < file.chunkCount)
        return 0;
    return count;
}

ResumeFile planFileResume(const FileState& file) {
    if (file.cancelled) return ResumeCancelled;
    if (file.complete && (!file.serverCache || file.outgoing)) return ResumeFinished;
    if (file.transportVersion != 3) return ResumeLegacy;
    if (file.complete) return ResumeCacheCopy;
    return ResumeExisting;
}

void startCacheCopy(FileState& file) {
    file.complete = false;
    file.cacheCopy = true;
    file.nextContiguousChunk = 0;
    file.previousTag.clear();
    file.partialDigest.clear();
}

bool skipFileTick(const FileState& file) {
    return file.paused || file.cancelled || file.transportVersion != 3 ||
           (file.outgoing && file.cacheUploaded && !file.complete);
}

bool wantsCompletionAck(const FileState& file, const ccfw::WireEnvelope* lastAck, const std::string& ackId) {
    return !file.outgoing && !file.cacheCopy && (!lastAck || lastAck->messageId != ackId);
}

bool shouldFetchCache(const FileState& file, bool downloadInFlight, bool buffered, int64_t sinceRequest) {
    return file.serverCache && file.nextContiguousChunk < file.chunkCount &&
           !downloadInFlight && !buffered && sinceRequest >= 250;
}

bool cacheReadExpired(int64_t elapsed) {
    return elapsed >= 30000;
}

bool cacheErrorExpired(FileRuntimeState& runtime, int64_t now) {
    if (!runtime.cacheFailureSince) runtime.cacheFailureSince = now;
    return cacheReadExpired(now - runtime.cacheFailureSince);
}

bool shouldFlushReceiveWindow(const FileRuntimeState& runtime, int64_t now) {
    return runtime.bufferedSince && now - runtime.bufferedSince >= 100;
}

// 所有新块先持久化，窗口最前面的超时块才进入退避重发。
bool pumpSendWindow(FileState& file, FileRuntimeState& runtime, int& budget, SendWindow& port) {
    bool changed = false;
    while (budget > 0 && file.pendingChunks.size() < kSendWindow &&
           file.nextContiguousChunk + file.pendingChunks.size() < file.chunkCount) {
        --budget;
        const uint32_t sequence = file.nextContiguousChunk + static_cast<uint32_t>(file.pendingChunks.size());
        const uint64_t offset = uint64_t(sequence) * kFileChunkSize;
        file.pendingChunks.push_back(port.prepare(sequence, offset, bytesInChunk(file.totalBytes, sequence)));
        changed = true;
    }
    if (changed && !port.persist()) return false;
    int sends = kSendPacketsPerTick;
    for (std::size_t i = 0; i < file.pendingChunks.size() && sends > 0; ++i) {
        const uint32_t sequence = file.nextContiguousChunk + static_cast<uint32_t>(i);
        const SentBlocks::const_iterator previous = runtime.sentAt.find(sequence);
        const int64_t now = port.clock();
        const bool retry = previous != runtime.sentAt.end();
        if (retry && (i != 0 || now - previous->second < runtime.retryDelay)) continue;
        if (!port.submit(i, sequence)) return false;
        runtime.sentAt[sequence] = now;
        --sends;
        if (retry) runtime.retryDelay = std::min(kMaxRetryMs, runtime.retryDelay * 2);
    }
    return true;
}

// 校验连续块与整份文件，提交检查点后才回文件签收和中转签收。
ReceiveBatchStatus pumpReceiveWindow(FileState& file, FileRuntimeState& runtime,
    BufferedBlocks& queue, std::vector<ccfw::ReceivedEnvelope>& receipts,
    std::size_t count, ReceiveWindow& port) {
    const uint32_t first = file.nextContiguousChunk;
    std::string data;
    std::vector<ccfw::WireEnvelope> committed;
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t sequence = file.nextContiguousChunk;
        const BufferedBlocks::iterator entry = queue.find(sequence);
        if (entry == queue.end()) return BatchInvalidBlock;
        const ccfw::WireEnvelope& packet = entry->second;
        std::string bytes;
        std::string tag;
        std::string packetDigest;
        if (!port.decode(packet, bytes, tag, packetDigest) ||
            bytes.size() != bytesInChunk(file.totalBytes, sequence)) {
            queue.erase(entry);
            return BatchInvalidBlock;
        }
        port.addToDigest(bytes);
        data += bytes;
        ++file.nextContiguousChunk;
        file.previousTag = tag;
        rememberReceipt(receipts, packet, packetDigest);
        committed.push_back(packet);
    }
    const bool last = file.nextContiguousChunk == file.chunkCount;
    file.partialDigest = port.digest();
    if (last && file.partialDigest != file.sourceDigest) return BatchBadDigest;
    if (!port.writeAndPersist(uint64_t(first) * kFileChunkSize, data)) return BatchIoFailed;
    for (std::size_t i = 0; i < count; ++i)
        queue.erase(first + static_cast<uint32_t>(i));
    runtime.bufferedSince = queue.empty() ? 0 : port.clock();
    port.publish();
    if (last && !port.finish()) return BatchIoFailed;
    if (!port.sendFileAck()) return BatchIoFailed;
    for (std::size_t i = 0; i < committed.size(); ++i) port.sendRelayAck(committed[i]);
    return BatchDone;
}

} // namespace chat
} // namespace nextalk
