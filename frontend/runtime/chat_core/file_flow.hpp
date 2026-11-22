// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_FILE_FLOW_HPP
#define NEXTALK_CHAT_FILE_FLOW_HPP

#include "message_records.hpp"
#include <map>

namespace nextalk {
namespace chat {

extern const uint64_t kMaxFileBytes;
extern const uint32_t kFileChunkSize;
extern const uint32_t kMetadataAck;

// 持久化字段供业务核心共享，平台路径和序列化留在适配层。
struct FileState {
    std::string fileId;
    std::string peer;
    uint64_t totalBytes;
    uint32_t nextContiguousChunk;
    std::string fileKey;
    std::string previousTag;
    std::string sourceDigest;
    bool outgoing;
    bool metadataQueued;
    bool metadataAcknowledged;
    bool complete;
    bool cancelled;
    std::string name;
    uint32_t chunkCount;
    std::string partialDigest;
    uint32_t transportVersion;
    std::vector<ccfw::PendingFileChunk> pendingChunks;
    uint8_t path;
    int64_t timestamp;
    bool serverCache;
    bool cacheUploaded;
    bool cacheCopy;
    bool paused;
    int64_t conversationGroupId;

    FileState();
};

typedef std::map<uint32_t, int64_t> SentBlocks;

struct FileRuntimeState {
    uint64_t offset;
    uint64_t limit;
    bool ready;
    bool failed;
    int64_t lastSend;
    int64_t bufferedSince;
    int64_t retryDelay;
    SentBlocks sentAt;
    bool cacheReady;
    int64_t cacheRequest;
    int64_t cacheFailureSince;

    FileRuntimeState();
    void resetCacheConnection();
};

typedef std::map<uint32_t, ccfw::WireEnvelope> BufferedBlocks;
typedef std::map<std::string, BufferedBlocks> ReceiveQueues;

uint32_t chunkCount(uint64_t bytes);
uint64_t verifiedBytes(const FileState& file);
std::size_t bytesInChunk(uint64_t size, uint32_t sequence);
std::string safeDownloadName(const std::string& name);
bool mayStartFile(const FileState& file, bool sending);
bool sameIncomingFile(const FileState& previous, const FileState& received);
bool validChunkRoute(const FileState& file, const ccfw::WireEnvelope& packet,
                     const std::string& owner, uint32_t sequence);

enum AckUpdate { AckApplied, AckRepeated, AckOutsideFile, AckOutsideWindow, AckMetadataNotQueued };
AckUpdate applyFileAck(FileState& file, uint32_t sequence);
void discardSentPrefix(SentBlocks& sent, uint32_t nextSequence);
bool applyCachePosition(FileState& file, FileRuntimeState& runtime, bool begin, bool complete,
                        uint32_t nextSequence);

enum ReceiveChunkStatus { ChunkBuffered, ChunkRepeated, ChunkConflict, ChunkTooFar, ChunkBufferFull };
ReceiveChunkStatus bufferChunk(ReceiveQueues& queues, const FileState& file, uint32_t sequence,
                               const ccfw::WireEnvelope& packet, FileRuntimeState* runtime, int64_t now);
bool hasBufferedChunks(const ReceiveQueues& queues);
std::size_t readyBatch(const FileState& file, const BufferedBlocks& blocks, bool force, std::size_t budget);

enum ResumeFile { ResumeCancelled, ResumeFinished, ResumeLegacy, ResumeCacheCopy, ResumeExisting };
ResumeFile planFileResume(const FileState& file);
void startCacheCopy(FileState& file);
bool skipFileTick(const FileState& file);
bool wantsCompletionAck(const FileState& file, const ccfw::WireEnvelope* lastAck, const std::string& ackId);
bool shouldFetchCache(const FileState& file, bool downloadInFlight, bool buffered,
                      int64_t sinceRequest);
bool cacheReadExpired(int64_t elapsed);
bool cacheErrorExpired(FileRuntimeState& runtime, int64_t now);
bool shouldFlushReceiveWindow(const FileRuntimeState& runtime, int64_t now);

class SendWindow {
public:
    virtual ~SendWindow() {}
    virtual ccfw::PendingFileChunk prepare(uint32_t sequence, uint64_t offset, std::size_t size) = 0;
    virtual bool persist() = 0;
    virtual int64_t clock() const = 0;
    virtual bool submit(std::size_t index, uint32_t sequence) = 0;
};

bool pumpSendWindow(FileState& file, FileRuntimeState& runtime, int& budget, SendWindow& port);

class ReceiveWindow {
public:
    virtual ~ReceiveWindow() {}
    virtual bool decode(const ccfw::WireEnvelope& packet, std::string& bytes,
                        std::string& tag, std::string& packetDigest) = 0;
    virtual void addToDigest(const std::string& bytes) = 0;
    virtual std::string digest() = 0;
    virtual bool writeAndPersist(uint64_t offset, const std::string& bytes) = 0;
    virtual void publish() = 0;
    virtual bool finish() = 0;
    virtual bool sendFileAck() = 0;
    virtual void sendRelayAck(const ccfw::WireEnvelope& packet) = 0;
    virtual int64_t clock() const = 0;
};

enum ReceiveBatchStatus { BatchDone, BatchInvalidBlock, BatchBadDigest, BatchIoFailed };
ReceiveBatchStatus pumpReceiveWindow(FileState& file, FileRuntimeState& runtime,
    BufferedBlocks& queue, std::vector<ccfw::ReceivedEnvelope>& receipts,
    std::size_t count, ReceiveWindow& port);

} // namespace chat
} // namespace nextalk

#endif
