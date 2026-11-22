// 人工实现，由 ChatGPT 规范化。

#include "binary.hpp"
#include "delivery_flow.hpp"
#include "directory_store.hpp"
#include "file_cache_store.hpp"
#include "file_flow.hpp"
#include "relay_store.hpp"
#include "snapshot_store.hpp"
#include "sqlite_core.hpp"

#include <cstdio>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace nextalk::chat;

void expect(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

std::string fakeDigest(const std::string& bytes) {
    return "test:" + bytes;
}

struct TempFileGuard {
    std::string path;
    explicit TempFileGuard(const std::string& p) : path(p) { std::remove(path.c_str()); }
    ~TempFileGuard() { std::remove(path.c_str()); }
};

void testSqliteCore() {
    ccfw::sql::Database db;
    db.open("");
    expect(db.value != 0, "empty path opens in-memory sqlite db");
    ccfw::sql::exec(db.value, "CREATE TABLE kv (k TEXT PRIMARY KEY, v BLOB, n INTEGER)");

    {
        ccfw::sql::Transaction tx(db.value);
        ccfw::sql::Statement ins(db.value, "INSERT INTO kv VALUES (?1, ?2, ?3)");
        ins.text(1, std::string("alpha"));
        const std::string raw("a\0b", 3);
        ins.blob(2, raw);
        ins.number(3, 42);
        expect(!ins.step(), "insert step completes");
        // Leave transaction uncommitted to verify RAII rollback.
    }

    {
        ccfw::sql::Statement query(db.value, "SELECT count(*) FROM kv");
        expect(query.step() && query.number(0) == 0, "uncommitted transaction rolls back");
    }

    {
        ccfw::sql::Transaction tx(db.value);
        ccfw::sql::Statement ins(db.value, "INSERT INTO kv VALUES (?1, ?2, ?3)");
        const std::string raw("x\0y", 3);
        ins.text(1, "k1");
        ins.blob(2, raw);
        ins.number(3, 7);
        ins.step();
        ins.reset();
        ins.text(1, "k2");
        ins.blob(2, raw);
        ins.number(3, 9);
        ins.step();
        tx.commit();
    }

    ccfw::sql::Statement sel(db.value, "SELECT k, v, n FROM kv ORDER BY n");
    expect(sel.step() && sel.columnText(0) == "k1" &&
           sel.bytes(1) == std::string("x\0y", 3) && sel.number(2) == 7,
           "statement reads text, binary blob, and int64");
    expect(sel.step() && sel.columnText(0) == "k2" && sel.number(2) == 9,
           "statement reads second row after reset");
    expect(!sel.step(), "no third row");

    bool threw = false;
    try {
        ccfw::sql::exec(db.value, "INVALID SQL SYNTAX");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "invalid SQL throws std::runtime_error");

    ccfw::sql::Database other;
    db.swap(other);
    expect(db.value == 0 && other.value != 0, "Database::swap exchanges ownership");
}

void testRelayStore() {
    RelayStore store;
    const PendingMessage second("bob", "alice", "m2", "payload-2");
    const PendingMessage first("bob", "alice", "m1", "payload-1");
    expect(store.enqueue(second, 1, fakeDigest) == QueueClosed, "closed store rejects enqueue");
    expect(store.open(":memory:") == QueueOk, "queue opens");

    const PendingMessage invalid("", "alice", "m0", "x");
    expect(store.enqueue(invalid, 1, fakeDigest) == QueueInvalid, "invalid message rejected");
    expect(store.enqueue(second, 5, fakeDigest) == QueueOk, "queue first insertion");
    expect(store.enqueue(first, 1, fakeDigest) == QueueOk, "queue second insertion");
    expect(store.enqueue(second, 6, fakeDigest) == QueueOk, "pending enqueue is idempotent");

    const PendingMessage conflict("bob", "alice", "m2", "another payload");
    expect(store.enqueue(conflict, 6, fakeDigest) == QueueConflict, "pending conflict rejected");

    bool found = false;
    PendingMessage foundMsg;
    expect(store.find("bob", "m2", found, foundMsg) == QueueOk && found &&
           foundMsg.payload == "payload-2", "find locates pending message");

    std::vector<PendingMessage> pending;
    expect(store.peek("bob", 5000, pending) == QueueInvalid, "peek over max peer limit rejected");
    expect(store.peek("bob", 64, pending) == QueueOk && pending.size() == 2 &&
           pending[0].messageId == "m2" && pending[1].messageId == "m1",
           "peek preserves SQLite insertion order");

    expect(store.acknowledge("eve", "m2", fakeDigest) == QueueOk, "unknown ack is safe no-op");
    expect(store.acknowledge("bob", "m2", fakeDigest) == QueueOk, "persist acknowledgement");
    expect(store.find("bob", "m2", found, foundMsg) == QueueOk && !found,
           "acknowledged item deleted from pending queue");
    expect(store.enqueue(second, 7, fakeDigest) == QueueAlreadyProcessed,
           "replay of acknowledged message reports QueueAlreadyProcessed");
    expect(store.enqueue(conflict, 7, fakeDigest) == QueueConflict,
           "acknowledged message with different payload conflicts");
    expect(store.peek("bob", 64, pending) == QueueOk && pending.size() == 1 &&
           pending[0].messageId == "m1", "only acknowledged item removed");
}

void testSnapshotStore() {
    std::set<std::string> files;
    files.insert("transfer");
    expect(legacyFilePacket(files, "file_meta", "fmeta:transfer") &&
           legacyFilePacket(files, "file_ack", "fack:transfer:2") &&
           !legacyFilePacket(files, "file_chunk", "fchunk:transfer-other:2") &&
           !legacyFilePacket(files, "chat", "fmeta:transfer"),
           "legacyFilePacket matches exact file IDs and kinds");

    expect(validSnapshotRevision(0, 1), "valid revision increment");
    expect(!validSnapshotRevision(0, 2), "reject skipped revision");
    expect(!validSnapshotRevision(static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max()), 0),
           "reject revision overflow");

    ccfw::sql::Database db;
    db.open(":memory:");
    initializeSnapshot(db.value, "endpoint", "alice", "initial");
    expect(ensureSnapshotTables(db.value), "snapshot schema version 1 valid");

    SnapshotRow row;
    expect(!readSnapshot(db.value, "endpoint", "other", row), "wrong user rejected");
    expect(!readSnapshot(db.value, "wrong-srv", "alice", row), "wrong server rejected");
    expect(readSnapshot(db.value, "endpoint", "alice", row) && row.revision == 0 &&
           row.mainBytes == "initial" && row.featureBytes.empty(),
           "legacy database without client_features still reads");

    row.revision = 1;
    row.mainBytes = "outbox";
    row.featureBytes = "features";
    {
        ccfw::sql::Transaction transaction(db.value);
        expect(updateSnapshot(db.value, "endpoint", "alice", 0, row), "conditional write");
    }
    SnapshotRow current;
    expect(readSnapshot(db.value, "endpoint", "alice", current) &&
           current.revision == 0 && current.featureBytes.empty(),
           "uncommitted CAS update rolls back both tables");
    {
        ccfw::sql::Transaction transaction(db.value);
        expect(updateSnapshot(db.value, "endpoint", "alice", 0, row), "write before commit");
        transaction.commit();
    }
    expect(!updateSnapshot(db.value, "endpoint", "alice", 0, row), "stale revision rejected");
    expect(readSnapshot(db.value, "endpoint", "alice", current) &&
           current.revision == 1 && current.mainBytes == "outbox" &&
           current.featureBytes == "features",
           "committed snapshot and features persisted");

    TempFileGuard archiveGuard("cpp98_snapshot_archive_test.db");
    {
        ccfw::sql::Database fileDb;
        fileDb.open(archiveGuard.path);
        initializeSnapshot(fileDb.value, "srv", "bob", "archived-blob");
    }
    std::string archived;
    expect(readArchivedSnapshot(archiveGuard.path, archived) && archived == "archived-blob",
           "readArchivedSnapshot extracts protected_snapshot");
    expect(!readArchivedSnapshot("nonexistent_snapshot_999.db", archived),
           "missing archive returns false");
}

void setupDirectorySchema(sqlite3* db) {
    ccfw::sql::exec(db,
        "CREATE TABLE accounts (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE NOT NULL);"
        "CREATE TABLE groups (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1);"
        "CREATE TABLE group_members (group_id INTEGER NOT NULL, username TEXT NOT NULL);"
        "CREATE TABLE group_former_members (group_id INTEGER NOT NULL, username TEXT NOT NULL);"
        "CREATE TABLE group_owners (group_id INTEGER NOT NULL, username TEXT NOT NULL);"
        "CREATE TABLE friend_requests (sender INTEGER NOT NULL, recipient INTEGER NOT NULL, PRIMARY KEY(sender, recipient));"
        "CREATE TABLE friends (user_id INTEGER NOT NULL, friend_id INTEGER NOT NULL, PRIMARY KEY(user_id, friend_id));"
        "CREATE TABLE offline_messages (id INTEGER PRIMARY KEY AUTOINCREMENT, to_user INTEGER NOT NULL, sender TEXT NOT NULL, payload TEXT NOT NULL);");
    ccfw::sql::exec(db,
        "INSERT INTO accounts(username) VALUES ('alice'),('bob'),('carol'),('dave'),('eve');");
}

void testDirectoryStore() {
    ccfw::sql::Database db;
    db.open(":memory:");
    setupDirectorySchema(db.value);
    DirectoryStore dir(db.value);

    std::vector<std::string> members;
    members.push_back("alice");
    members.push_back("bob");
    int64_t groupId = 0;
    expect(!dir.createGroup("Solo", std::vector<std::string>(1, "alice"), groupId),
           "reject group with fewer than 2 members");
    expect(dir.createGroup("StudyGroup", members, groupId) && groupId > 0, "create group succeeds");

    GroupRow foundGroup;
    expect(dir.findGroup(groupId, foundGroup) && foundGroup.name == "StudyGroup" &&
           foundGroup.revision == 1 && foundGroup.members.size() == 2,
           "findGroup loads group details");

    ccfw::sql::exec(db.value, "INSERT INTO group_former_members VALUES (1, 'carol')");
    const std::vector<GroupRow> carolGroups = dir.groupsForUser("carol");
    expect(carolGroups.size() == 1 && carolGroups[0].id == groupId && carolGroups[0].members.empty(),
           "former member receives empty-member revocation record");

    expect(dir.requestFriend("alice", "alice").empty(), "self friend request rejected");
    expect(dir.requestFriend("alice", "unknown").empty(), "missing user rejected");
    expect(dir.requestFriend("alice", "bob") == "pending", "friend request created");
    expect(dir.requestFriend("alice", "bob") == "pending", "duplicate request stays pending");
    expect(dir.friendRequests("bob", true).size() == 1 && dir.friendRequests("bob", true)[0] == "alice",
           "incoming friend request visible");
    expect(dir.friendRequests("alice", false).size() == 1 && dir.friendRequests("alice", false)[0] == "bob",
           "outgoing friend request visible");

    expect(dir.respondFriend("bob", "alice", true), "accept friend request");
    expect(dir.requestFriend("alice", "bob") == "friends", "existing friendship reported");
    expect(dir.friendsForUser("alice").size() == 1 && dir.friendsForUser("alice")[0] == "bob",
           "alice friends list updated");
    expect(dir.friendsForUser("bob").size() == 1 && dir.friendsForUser("bob")[0] == "alice",
           "bob friends list updated");

    expect(dir.requestFriend("carol", "alice") == "pending", "carol requests alice");
    expect(dir.respondFriend("alice", "carol", false), "reject friend request");
    expect(dir.friendRequests("alice", true).empty() && dir.friendsForUser("carol").empty(),
           "rejected request cleared without adding friendship");

    const std::vector<std::string> hits = dir.searchUsernames("alice", "A", "");
    expect(hits.size() == 2 && hits[0] == "carol" && hits[1] == "dave",
           "searchUsernames filters out self and existing friends, case-insensitive");
    const std::vector<std::string> paged = dir.searchUsernames("alice", "", "carol");
    expect(paged.size() == 2 && paged[0] == "dave" && paged[1] == "eve",
           "searchUsernames respects cursor pagination");

    dir.enqueueOffline(2, "alice", "msg-1");
    dir.enqueueOffline(2, "carol", "msg-2");
    const std::vector<OfflineRow> drained = dir.drainOffline(2);
    expect(drained.size() == 2 && drained[0].sender == "alice" && drained[0].payload == "msg-1" &&
           drained[1].sender == "carol" && drained[1].payload == "msg-2",
           "drainOffline preserves order");
    expect(dir.drainOffline(2).empty(), "drainOffline deletes drained rows");
}

void testFileCacheStore() {
    FileCacheStore cache(100, 1000);
    cache.open(":memory:");

    const CachePosition pos0 = cache.begin("alice", "f1", "bob", 70000, 2, 100);
    expect(pos0.next == 0 && !pos0.complete, "begin initializes file at chunk 0");

    bool mismatchThrew = false;
    try {
        cache.begin("alice", "f1", "carol", 70000, 2, 101);
    } catch (const std::runtime_error&) {
        mismatchThrew = true;
    }
    expect(mismatchThrew, "begin rejects mismatched recipient for same file id");

    const std::string c0 = std::string("CCP4") + std::string(40, 'a');
    const std::string c1 = std::string("CCP4") + std::string(40, 'b');
    expect(cache.put("alice", "f1", 0, c0, 200) == 1, "put chunk 0 advances next");
    expect(cache.put("alice", "f1", 0, c0, 201) == 1, "idempotent retry accepted");

    bool badPrefixThrew = false;
    try {
        cache.put("alice", "f1", 1, "BAD0xxxx", 202);
    } catch (const std::runtime_error&) {
        badPrefixThrew = true;
    }
    expect(badPrefixThrew, "put rejects chunk without CCP4 magic");

    bool conflictThrew = false;
    try {
        cache.put("alice", "f1", 0, c1, 203);
    } catch (const std::runtime_error&) {
        conflictThrew = true;
    }
    expect(conflictThrew, "put rejects conflicting data for existing chunk");

    expect(cache.put("alice", "f1", 1, c1, 204) == 2, "put chunk 1 completes file");
    const CachePosition posDone = cache.begin("alice", "f1", "bob", 70000, 2, 205);
    expect(posDone.next == 2 && posDone.complete, "resume reports complete cache");

    std::string out;
    expect(cache.get("bob", "alice", "f1", 0, 206, out) && out == c0, "recipient reads chunk 0");
    expect(cache.get("alice", "alice", "f1", 1, 206, out) && out == c1, "owner reads chunk 1");
    bool expiredThrew = false;
    try {
        cache.get("bob", "alice", "f1", 0, 1200, out);
    } catch (const std::runtime_error&) {
        expiredThrew = true;
    }
    expect(expiredThrew, "get rejects expired file");

    bool thirdPartyThrew = false;
    try {
        cache.get("eve", "alice", "f1", 0, 207, out);
    } catch (const std::runtime_error&) {
        thirdPartyThrew = true;
    }
    expect(thirdPartyThrew, "get denies unauthorized third party");

    // Capacity is 100 bytes; f1 uses 88 bytes, so another 44-byte chunk fails until pruned/cancelled.
    cache.begin("alice", "f2", "bob", 10, 1, 300);
    bool capThrew = false;
    try {
        cache.put("alice", "f2", 0, c0, 301);
    } catch (const std::runtime_error&) {
        capThrew = true;
    }
    expect(capThrew, "capacity limit enforced across files");

    cache.cancel("alice", "f1");
    expect(cache.put("alice", "f2", 0, c0, 302) == 1, "cancel frees capacity for new upload");

    // Expire f2 (expires at 300 + 1000 = 1300) and prune during next begin at t=1300.
    const CachePosition pos3 = cache.begin("alice", "f3", "bob", 10, 1, 1300);
    expect(pos3.next == 0, "prune runs on begin");
    expect(cache.put("alice", "f3", 0, c0, 1301) == 1, "expired f2 freed quota");
}

class RecordingSender : public SendWindow {
public:
    explicit RecordingSender(bool save) : now(500), submitOk(true), save_(save) {}
    ccfw::PendingFileChunk prepare(uint32_t seq, uint64_t, std::size_t) {
        steps += 'P';
        std::ostringstream id;
        id << "fchunk:f1:" << seq;
        return ccfw::PendingFileChunk(
            ccfw::WireEnvelope(id.str(), "alice", "bob", "file_chunk", "AA=="), "tag");
    }
    bool persist() {
        steps += 'S';
        return save_;
    }
    int64_t clock() const { return now; }
    bool submit(std::size_t, uint32_t) {
        steps += 'T';
        return submitOk;
    }
    std::string steps;
    int64_t now;
    bool submitOk;
private:
    bool save_;
};

class RecordingReceiver : public ReceiveWindow {
public:
    explicit RecordingReceiver(bool save)
        : decodeOk(true), reportedDigest("file digest"), finishOk(true), save_(save) {}
    bool decode(const ccfw::WireEnvelope&, std::string& bytes,
                std::string& tag, std::string& packetDigest) {
        steps += 'D';
        if (!decodeOk) return false;
        bytes = "file";
        tag = "tag";
        packetDigest = "packet digest";
        return true;
    }
    void addToDigest(const std::string&) {}
    std::string digest() { return reportedDigest; }
    bool writeAndPersist(uint64_t offset, const std::string& bytes) {
        expect(offset == 0 && bytes == "file", "written bytes and offset");
        steps += 'S';
        return save_;
    }
    void publish() { steps += 'P'; }
    bool finish() { steps += 'F'; return finishOk; }
    bool sendFileAck() { steps += 'A'; return true; }
    void sendRelayAck(const ccfw::WireEnvelope&) { steps += 'R'; }
    int64_t clock() const { return 500; }
    std::string steps;
    bool decodeOk;
    std::string reportedDigest;
    bool finishOk;
private:
    bool save_;
};

void testFileFlow() {
    expect(chunkCount(0) == 0 && chunkCount(1) == 1 &&
           chunkCount(kFileChunkSize) == 1 && chunkCount(kFileChunkSize + 1) == 2,
           "chunkCount boundary calculation");
    expect(bytesInChunk(kFileChunkSize + 5, 0) == kFileChunkSize &&
           bytesInChunk(kFileChunkSize + 5, 1) == 5 &&
           bytesInChunk(kFileChunkSize + 5, 2) == 0,
           "bytesInChunk handles full, tail, and out-of-range");
    expect(safeDownloadName("a<b:c>.. ") == "received_a_b_c_", "safeDownloadName sanitizes reserved chars");
    expect(safeDownloadName("...") == "received_download", "safeDownloadName falls back when stripped empty");

    FileState ackState;
    ackState.totalBytes = kFileChunkSize * 2;
    ackState.chunkCount = 2;
    expect(applyFileAck(ackState, kMetadataAck) == AckMetadataNotQueued,
           "metadata ack requires queued metadata");
    ackState.metadataQueued = true;
    expect(applyFileAck(ackState, kMetadataAck) == AckApplied && ackState.metadataAcknowledged,
           "metadata ack applied");
    expect(applyFileAck(ackState, kMetadataAck) == AckRepeated, "duplicate metadata ack repeated");
    ackState.pendingChunks.push_back(ccfw::PendingFileChunk(ccfw::WireEnvelope(), "tag0"));
    ackState.pendingChunks.push_back(ccfw::PendingFileChunk(ccfw::WireEnvelope(), "tag1"));
    expect(applyFileAck(ackState, 2) == AckOutsideFile, "out-of-file ack rejected");
    expect(applyFileAck(ackState, 1) == AckApplied && ackState.complete &&
           ackState.nextContiguousChunk == 2 && ackState.previousTag == "tag1" &&
           ackState.pendingChunks.empty(),
           "cumulative ack advances window and completes file");
    expect(applyFileAck(ackState, 0) == AckRepeated, "prior chunk ack reported repeated");

    FileState file;
    file.fileId = "f1";
    file.peer = "alice";
    file.totalBytes = 4;
    file.chunkCount = 1;
    file.sourceDigest = "file digest";
    expect(mayStartFile(file, true) && validChunkRoute(file,
           ccfw::WireEnvelope("fchunk:f1:0", "alice", "bob", "file_chunk", "AA=="), "bob", 0),
           "mayStartFile and validChunkRoute");

    ReceiveQueues queues;
    FileRuntimeState rt;
    const ccfw::WireEnvelope pkt("fchunk:f1:0", "alice", "bob", "file_chunk", "AA==");
    expect(bufferChunk(queues, file, 0, pkt, &rt, 100) == ChunkBuffered && hasBufferedChunks(queues),
           "bufferChunk stores packet and records timestamp");
    expect(bufferChunk(queues, file, 0, pkt, &rt, 101) == ChunkRepeated, "identical chunk repeated");
    expect(bufferChunk(queues, file, 0,
           ccfw::WireEnvelope("fchunk:f1:0", "alice", "bob", "file_chunk", "AQ=="), &rt, 102) == ChunkConflict,
           "different payload conflicts");
    expect(bufferChunk(queues, file, 64, pkt, &rt, 103) == ChunkTooFar, "sequence beyond window rejected");
    expect(readyBatch(file, queues["f1"], false, 4) == 1, "tail chunk ready without force");
    expect(shouldFlushReceiveWindow(rt, 200) && !shouldFlushReceiveWindow(rt, 199),
           "flush receive window after 100ms");

    FileState outgoing = file;
    FileRuntimeState sending;
    int budget = 1;
    RecordingSender failPersist(false);
    expect(!pumpSendWindow(outgoing, sending, budget, failPersist) && failPersist.steps == "PS",
           "persistence failure stops before submit");

    outgoing = file;
    budget = 1;
    RecordingSender sender(true);
    expect(pumpSendWindow(outgoing, sending, budget, sender) && sender.steps == "PST" &&
           sending.sentAt[0] == 500, "send after persistence records timestamp");
    sender.now = 1000;
    expect(pumpSendWindow(outgoing, sending, budget, sender) && sender.steps == "PST",
           "does not re-send before retryDelay");
    sender.now = 3500;
    expect(pumpSendWindow(outgoing, sending, budget, sender) && sender.steps == "PSTT" &&
           sending.retryDelay == 6000, "re-sends after retryDelay and doubles backoff");

    {
        FileState recvFile = file;
        FileRuntimeState receiving;
        BufferedBlocks queue;
        queue[0] = pkt;
        std::vector<ccfw::ReceivedEnvelope> receipts;
        RecordingReceiver failIo(false);
        expect(pumpReceiveWindow(recvFile, receiving, queue, receipts, 1, failIo) == BatchIoFailed &&
               failIo.steps == "DS" && !queue.empty(),
               "IO failure prevents file ack and relay ack");
    }
    {
        FileState recvFile = file;
        FileRuntimeState receiving;
        BufferedBlocks queue;
        queue[0] = pkt;
        std::vector<ccfw::ReceivedEnvelope> receipts;
        RecordingReceiver badDigest(true);
        badDigest.reportedDigest = "mismatch";
        expect(pumpReceiveWindow(recvFile, receiving, queue, receipts, 1, badDigest) == BatchBadDigest &&
               badDigest.steps == "D",
               "whole-file digest mismatch returns BatchBadDigest before IO");
    }
    {
        FileState recvFile = file;
        FileRuntimeState receiving;
        BufferedBlocks queue;
        queue[0] = pkt;
        std::vector<ccfw::ReceivedEnvelope> receipts;
        RecordingReceiver receiver(true);
        expect(pumpReceiveWindow(recvFile, receiving, queue, receipts, 1, receiver) == BatchDone &&
               receiver.steps == "DSPFAR" && queue.empty() && receipts.size() == 1,
               "valid receive batch writes, publishes, finishes, and emits both acks");
    }

    expect(planFileResume(file) == ResumeExisting, "resume existing incomplete file");
    file.cancelled = true;
    expect(planFileResume(file) == ResumeCancelled, "cancelled resume plan");
    expect(skipFileTick(file), "cancelled file skips tick");
    expect(cacheReadExpired(30000) && !cacheReadExpired(29999), "cache timeout at 30000ms");
}

void testDeliveryFlow() {
    expect(canCreateDelivery("alice", "b1", "bob", "Release", "Notes", 2, 0),
           "canCreateDelivery accepts valid metadata");
    expect(!canCreateDelivery("alice", "b1", "alice", "Release", "Notes", 2, 0),
           "canCreateDelivery rejects self recipient");
    expect(!canCreateDelivery("alice", "b1", "bob", "Release", "Notes", 17, 0),
           "canCreateDelivery enforces max 16 items");

    ccfw::DeliveryBundle bundle;
    bundle.id = "bundle-1";
    bundle.title = "Release";
    bundle.description = "Desc";
    bundle.sender = "alice";
    bundle.recipient = "bob";
    bundle.manifestDigest = "manifest-sha";

    ccfw::DeliveryItem item1;
    item1.id = "entry-1";
    item1.name = "a.bin";
    item1.size = 100;
    const std::string digest(32, 'h');
    const std::string idBytes(16, 'i');
    item1.sha256 = base64Encode(reinterpret_cast<const uint8_t*>(digest.data()), digest.size());
    item1.transferId = base64Encode(reinterpret_cast<const uint8_t*>(idBytes.data()), idBytes.size());

    ccfw::DeliveryItem item2 = item1;
    item2.id = "entry-2";
    item2.name = "b.bin";
    item2.size = 300;
    const std::string idBytes2(16, 'j');
    item2.transferId = base64Encode(reinterpret_cast<const uint8_t*>(idBytes2.data()), idBytes2.size());

    bundle.items.push_back(item1);
    bundle.items.push_back(item2);
    expect(matchesDeliveryRequest(bundle, "alice", "bob", "Release", "Desc", "", 2),
           "matchesDeliveryRequest verifies request metadata");
    expect(validManifest(bundle) && validStoredDelivery(bundle) && canAnnounceDelivery(bundle),
           "valid two-item manifest");
    expect(permitsDeliveryFile(bundle, "alice", "bob", "entry-1", item1.transferId) &&
           !permitsDeliveryFile(bundle, "alice", "bob", "entry-1", item2.transferId),
           "permitsDeliveryFile binds item ID to transfer ID");

    bundle.items[1].transferId = item1.transferId;
    expect(!validManifest(bundle), "duplicate transfer IDs rejected");
    bundle.items[1].transferId = item2.transferId;

    bundle.announced = true;
    expect(!canAnnounceDelivery(bundle), "already announced cannot re-announce");

    FileState t1;
    t1.outgoing = false;
    t1.peer = "alice";
    t1.totalBytes = 100;
    t1.sourceDigest = item1.sha256;
    t1.nextContiguousChunk = 1;
    t1.complete = true;
    updateDeliveryFile(bundle, "bob", bundle.items[0], t1);
    updateDeliveryState(bundle);
    expect(bundle.state == ccfw::BundleState::Transferring,
           "one of two items complete keeps state Transferring");
    expect(deliveryProgress(bundle) == 0.25, "weighted byte progress 100/400 = 0.25");

    FileState t2 = t1;
    t2.totalBytes = 300;
    t2.sourceDigest = "wrong";
    updateDeliveryFile(bundle, "bob", bundle.items[1], t2);
    expect(!bundle.items[1].error.empty() && !shouldStartDeliveryFile(bundle.items[1], false),
           "digest mismatch marks item error");

    bundle.items[1].error.clear();
    t2.sourceDigest = item2.sha256;
    updateDeliveryFile(bundle, "bob", bundle.items[1], t2);
    updateDeliveryState(bundle);
    expect(bundle.state == ccfw::BundleState::Complete && deliveryProgress(bundle) == 1.0 &&
           completeDeliveryFile(bundle, bundle.items[1], t2),
           "both verified items complete bundle");

    expect(canConfirmDelivery(bundle, "bob", "manifest-sha") &&
           !canConfirmDelivery(bundle, "alice", "manifest-sha") &&
           !canConfirmDelivery(bundle, "bob", "other-sha"),
           "canConfirmDelivery validates receiver and manifest digest");
    bundle.manuallyConfirmed = true;
    updateDeliveryState(bundle);
    expect(bundle.state == ccfw::BundleState::Confirmed, "manual confirmation updates state");
}
} // namespace

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        { "sqlite_core", testSqliteCore },
        { "relay_store", testRelayStore },
        { "snapshot_store", testSnapshotStore },
        { "directory_store", testDirectoryStore },
        { "file_cache_store", testFileCacheStore },
        { "file_flow", testFileFlow },
        { "delivery_flow", testDeliveryFlow }
    };
    const std::size_t total = sizeof(cases) / sizeof(cases[0]);
    for (std::size_t i = 0; i < total; ++i) {
        try {
            cases[i].fn();
            std::cout << "[PASS] " << cases[i].name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << cases[i].name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Passed " << total << "/" << total << " C++98 reliable delivery test suites.\n";
    return 0;
}
