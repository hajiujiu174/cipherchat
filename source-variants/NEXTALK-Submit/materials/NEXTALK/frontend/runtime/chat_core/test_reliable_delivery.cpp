// 人工实现，由 ChatGPT 规范化。

#include "binary.hpp"
#include "delivery_flow.hpp"
#include "group_flow.hpp"
#include "relay_flow.hpp"
#include "snapshot_store.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace nextalk::chat;

void expect(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

std::string fakeDigest(const std::string& bytes) {
    return "test:" + bytes;
}

void testQueue() {
    RelayStore store;
    const PendingMessage second("bob", "alice", "m2", "payload-2");
    const PendingMessage first("bob", "alice", "m1", "payload-1");
    expect(store.enqueue(second, 1, fakeDigest) == QueueClosed, "closed store");
    expect(store.open(":memory:") == QueueOk, "queue opens");
    expect(store.enqueue(second, 5, fakeDigest) == QueueOk, "queue first insertion");
    expect(store.enqueue(first, 1, fakeDigest) == QueueOk, "queue second insertion");
    expect(store.enqueue(second, 6, fakeDigest) == QueueOk, "pending idempotent");
    const PendingMessage conflict("bob", "alice", "m2", "another payload");
    expect(store.enqueue(conflict, 6, fakeDigest) == QueueConflict, "pending conflict");
    std::vector<PendingMessage> pending;
    expect(store.peek("bob", 64, pending) == QueueOk && pending.size() == 2 &&
           pending[0].messageId == "m2" && pending[1].messageId == "m1", "queue uses insertion order");

    expect(store.acknowledge("eve", "m2", fakeDigest) == QueueOk, "unknown acknowledgement");
    expect(store.acknowledge("bob", "m2", fakeDigest) == QueueOk, "persist acknowledgement");
    expect(store.enqueue(second, 7, fakeDigest) == QueueAlreadyProcessed, "already received");
    expect(store.enqueue(conflict, 7, fakeDigest) == QueueConflict, "receipt conflict");
    expect(store.peek("bob", 64, pending) == QueueOk && pending.size() == 1 &&
           pending[0].messageId == "m1", "only acknowledged item removed");
}

void testSnapshot() {
    std::set<std::string> files;
    files.insert("transfer");
    expect(legacyFilePacket(files, "file_meta", "fmeta:transfer") &&
           legacyFilePacket(files, "file_ack", "fack:transfer:2") &&
           !legacyFilePacket(files, "file_chunk", "fchunk:transfer-other:2") &&
           !legacyFilePacket(files, "chat", "fmeta:transfer"), "exact legacy file IDs");

    ccfw::sql::Database db;
    db.open(":memory:");
    initializeSnapshot(db.value, "endpoint", "alice", "initial");
    expect(ensureSnapshotTables(db.value), "snapshot schema");
    SnapshotRow row;
    expect(!readSnapshot(db.value, "endpoint", "other", row), "wrong account scope");
    expect(readSnapshot(db.value, "endpoint", "alice", row) && row.revision == 0 &&
           row.mainBytes == "initial" && row.featureBytes.empty(), "previous database without features");
    row.revision = 1;
    row.mainBytes = "outbox";
    row.featureBytes = "features";
    {
        ccfw::sql::Transaction transaction(db.value);
        expect(updateSnapshot(db.value, "endpoint", "alice", 0, row), "conditional write");
    }
    SnapshotRow current;
    expect(readSnapshot(db.value, "endpoint", "alice", current) &&
           current.revision == 0 && current.featureBytes.empty(), "both writes roll back");
    {
        ccfw::sql::Transaction transaction(db.value);
        expect(updateSnapshot(db.value, "endpoint", "alice", 0, row), "write before commit");
        transaction.commit();
    }
    expect(!updateSnapshot(db.value, "endpoint", "alice", 0, row), "stale revision");
    expect(readSnapshot(db.value, "endpoint", "alice", current) &&
           current.revision == 1 && current.mainBytes == "outbox" &&
           current.featureBytes == "features", "committed snapshot and features");
}

class RecordingSender : public SendWindow {
public:
    explicit RecordingSender(bool save) : save_(save) {}
    ccfw::PendingFileChunk prepare(uint32_t, uint64_t, std::size_t) {
        steps += 'P';
        return ccfw::PendingFileChunk();
    }
    bool persist() {
        steps += 'S';
        return save_;
    }
    int64_t clock() const { return 500; }
    bool submit(std::size_t, uint32_t) {
        steps += 'T';
        return true;
    }
    std::string steps;
private:
    bool save_;
};

class RecordingReceiver : public ReceiveWindow {
public:
    explicit RecordingReceiver(bool save) : save_(save) {}
    bool decode(const ccfw::WireEnvelope&, std::string& bytes,
                std::string& tag, std::string& packetDigest) {
        steps += 'D';
        bytes = "file";
        tag = "tag";
        packetDigest = "packet digest";
        return true;
    }
    void addToDigest(const std::string&) {}
    std::string digest() { return "file digest"; }
    bool writeAndPersist(uint64_t offset, const std::string& bytes) {
        expect(offset == 0 && bytes == "file", "written bytes and offset");
        steps += 'S';
        return save_;
    }
    void publish() { steps += 'P'; }
    bool finish() { steps += 'F'; return true; }
    bool sendFileAck() { steps += 'A'; return true; }
    void sendRelayAck(const ccfw::WireEnvelope&) { steps += 'R'; }
    int64_t clock() const { return 500; }
    std::string steps;
private:
    bool save_;
};

void checkWindow(bool save) {
    FileState file;
    file.totalBytes = 4;
    file.chunkCount = 1;
    file.sourceDigest = "file digest";
    FileState outgoing = file;
    FileRuntimeState sending;
    int budget = 1;
    RecordingSender sender(save);
    expect(pumpSendWindow(outgoing, sending, budget, sender) == save &&
           sender.steps == (save ? "PST" : "PS"), "send only after persistence");

    FileRuntimeState receiving;
    BufferedBlocks queue;
    queue[0] = ccfw::WireEnvelope("fchunk:sample:0", "alice", "bob", "file_chunk", "data");
    std::vector<ccfw::ReceivedEnvelope> receipts;
    RecordingReceiver receiver(save);
    expect(pumpReceiveWindow(file, receiving, queue, receipts, 1, receiver) ==
           (save ? BatchDone : BatchIoFailed), "receive batch outcome");
    expect(receiver.steps == (save ? "DSPFAR" : "DS") && queue.empty() == save,
           "only persisted file data can generate either acknowledgement");
}

void testDelivery() {
    ccfw::DeliveryBundle bundle;
    bundle.id = "bundle";
    bundle.title = "Files";
    bundle.sender = "alice";
    bundle.recipient = "bob";
    ccfw::DeliveryItem item;
    item.id = "entry";
    item.name = "sample.bin";
    item.size = 4;
    const std::string digest(32, 'h');
    const std::string id(16, 'i');
    item.sha256 = base64Encode(reinterpret_cast<const uint8_t*>(digest.data()), digest.size());
    item.transferId = base64Encode(reinterpret_cast<const uint8_t*>(id.data()), id.size());
    bundle.items.push_back(item);
    expect(validManifest(bundle) && canAnnounceDelivery(bundle), "valid one-item manifest");
    bundle.items.push_back(item);
    expect(!validManifest(bundle), "duplicate IDs in manifest");
    bundle.items.pop_back();
    bundle.announced = true;
    FileState checkpoint;
    checkpoint.peer = "alice";
    checkpoint.totalBytes = 4;
    checkpoint.sourceDigest = item.sha256;
    checkpoint.nextContiguousChunk = 1;
    checkpoint.complete = true;
    updateDeliveryFile(bundle, "bob", bundle.items.front(), checkpoint);
    updateDeliveryState(bundle);
    expect(bundle.state == ccfw::BundleState::Complete && ccfw::deliveryProgress(bundle) == 1 &&
           completeDeliveryFile(bundle, bundle.items.front(), checkpoint), "verified file completes bundle");
}
} // namespace

int main() {
    try {
        testQueue();
        testSnapshot();
        checkWindow(false);
        checkWindow(true);
        testDelivery();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
