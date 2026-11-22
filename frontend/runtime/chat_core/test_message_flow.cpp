// 人工实现，由 ChatGPT 规范化。

#include "binary.hpp"
#include "group_flow.hpp"
#include "message_flow.hpp"
#include "message_records.hpp"
#include "relay_flow.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace nextalk::chat;

void expect(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

class MockSender : public OutgoingText {
public:
    MockSender(bool saved, bool submitted) : saved_(saved), submitted_(submitted) {}
    bool saveToOutbox() {
        steps += 'S';
        return saved_;
    }
    bool submitSavedPacket() {
        steps += 'T';
        return submitted_;
    }
    std::string steps;
private:
    bool saved_;
    bool submitted_;
};

class MockReceiver : public IncomingText {
public:
    explicit MockReceiver(ReceiveOutcome outcome) : outcome_(outcome) {}
    ReceiveOutcome saveToHistory() {
        steps += 'S';
        return outcome_;
    }
    void acknowledge() { steps += 'A'; }
    std::string steps;
private:
    ReceiveOutcome outcome_;
};

void checkSendOrder(bool saved, bool submitted, SendOutcome expected, const char* steps) {
    MockSender sender(saved, submitted);
    expect(sendText(sender) == expected && sender.steps == steps,
           "unexpected sendText outcome or call order");
}

void checkReceiveOrder(ReceiveOutcome outcome, const char* steps) {
    MockReceiver receiver(outcome);
    expect(receiveText(receiver) == outcome && receiver.steps == steps,
           "unexpected receiveText outcome or call order");
}

void testMessageFlow() {
    expect(validTextRequest("bob", "hello", true), "valid normal text request");
    expect(validTextRequest(std::string(32, 'u'), std::string(65536, 'a'), true),
           "valid max-length recipient and text");
    expect(!validTextRequest("", "text", true), "reject empty recipient");
    expect(!validTextRequest(std::string(33, 'u'), "text", true), "reject overlong recipient");
    expect(!validTextRequest("bob", "", true), "reject empty text");
    expect(!validTextRequest("bob", std::string(65537, 'a'), true), "reject overlong text");
    expect(!validTextRequest("bob", "text", false), "reject invalid group");

    checkSendOrder(false, true, SaveFailed, "S");
    checkSendOrder(true, false, SavedForRetry, "ST");
    checkSendOrder(true, true, TransportQueued, "ST");

    checkReceiveOrder(ReceiveFailed, "S");
    checkReceiveOrder(NonText, "S");
    checkReceiveOrder(DuplicateText, "SA");
    checkReceiveOrder(TextSaved, "SA");
}

void testBinary() {
    const std::string empty;
    std::string decoded;
    expect(base64Encode(0, 0).empty(), "encode empty");
    expect(base64Decode("", 0, decoded) && decoded.empty(), "decode empty");

    const std::string one("f");
    const std::string enc1 = base64Encode(reinterpret_cast<const uint8_t*>(one.data()), one.size());
    expect(enc1 == "Zg==", "encode 1 byte with == padding");
    expect(base64Decode(enc1.data(), enc1.size(), decoded) && decoded == one, "decode 1 byte");

    const std::string two("fo");
    const std::string enc2 = base64Encode(reinterpret_cast<const uint8_t*>(two.data()), two.size());
    expect(enc2 == "Zm8=", "encode 2 bytes with = padding");
    expect(base64Decode(enc2.data(), enc2.size(), decoded) && decoded == two, "decode 2 bytes");

    std::string allBytes;
    allBytes.reserve(256);
    for (int i = 0; i < 256; ++i) allBytes.push_back(static_cast<char>(i));
    const std::string encAll = base64Encode(reinterpret_cast<const uint8_t*>(allBytes.data()), allBytes.size());
    expect(base64Decode(encAll.data(), encAll.size(), decoded) && decoded == allBytes,
           "roundtrip all 256 byte values");

    expect(!base64Decode("abc", 3, decoded), "reject non-multiple of 4");
    expect(!base64Decode("ab*=", 4, decoded), "reject invalid base64 character");
    expect(!base64Decode("a=aa", 4, decoded), "reject data after padding");
    expect(!base64Decode("a===", 4, decoded), "reject more than 2 padding chars");

    const std::string payload = "frame-payload";
    const std::string frame = encodeFrame(payload.data(), payload.size());
    expect(frame.size() == kFrameHeaderSize + payload.size(), "frame size includes 4-byte header");

    std::string outPayload;
    std::size_t consumed = 0;
    expect(decodeFrame(frame.data(), 3, outPayload, consumed) == FrameNeedMoreData,
           "need more data for partial header");
    expect(decodeFrame(frame.data(), frame.size() - 1, outPayload, consumed) == FrameNeedMoreData,
           "need more data for partial payload");

    const std::string zeroFrame = encodeFrame("", 0);
    expect(decodeFrame(zeroFrame.data(), zeroFrame.size(), outPayload, consumed) == FrameInvalid,
           "reject zero-length frame");

    char oversized[4];
    const uint32_t tooBig = kMaxFrameSize + 1;
    oversized[0] = static_cast<char>((tooBig >> 24) & 0xFF);
    oversized[1] = static_cast<char>((tooBig >> 16) & 0xFF);
    oversized[2] = static_cast<char>((tooBig >> 8) & 0xFF);
    oversized[3] = static_cast<char>(tooBig & 0xFF);
    expect(decodeFrame(oversized, 4, outPayload, consumed) == FrameTooLarge,
           "reject frame larger than kMaxFrameSize");

    const std::string combined = frame + encodeFrame("next", 4);
    expect(decodeFrame(combined.data(), combined.size(), outPayload, consumed) == FrameOk &&
           outPayload == payload && consumed == frame.size(),
           "decode first frame and return exact consumed offset");
    std::size_t secondConsumed = 0;
    expect(decodeFrame(combined.data() + consumed, combined.size() - consumed, outPayload, secondConsumed) == FrameOk &&
           outPayload == "next" && secondConsumed == 8,
           "decode second frame from remaining buffer");
}

class MockOutboxFlush : public OutboxFlush {
public:
    MockOutboxFlush(std::size_t totalQueued, std::size_t totalPending)
        : failSubmitAt(999), failRetryAt(999), queued_(totalQueued), pending_(totalPending) {
        for (std::size_t i = 0; i < totalQueued; ++i) eligibleFlags.push_back(true);
    }
    std::size_t queued() const { return queued_; }
    bool eligible(std::size_t index) const { return eligibleFlags[index]; }
    bool submit(std::size_t index) {
        if (submitted.size() == failSubmitAt) return false;
        submitted.push_back(index);
        return true;
    }
    std::size_t pending() const { return pending_; }
    bool pendingPeer(std::size_t index, std::string& peer) const {
        if (index >= peers.size() || peers[index].empty()) return false;
        peer = peers[index];
        return true;
    }
    bool retry(const std::string& peer) {
        if (retriedPeers.size() == failRetryAt) return false;
        retriedPeers.push_back(peer);
        return true;
    }
    std::vector<bool> eligibleFlags;
    std::vector<std::size_t> submitted;
    std::vector<std::string> peers;
    std::vector<std::string> retriedPeers;
    std::size_t failSubmitAt;
    std::size_t failRetryAt;
private:
    std::size_t queued_;
    std::size_t pending_;
};

void testMessageRecords() {
    expect(validMessageId("m1") && !validMessageId("") && !validMessageId(std::string(129, 'x')),
           "message id bounds");
    expect(supportedSessionKind("chat") && supportedSessionKind("file_meta") &&
           supportedSessionKind("file_ack") && !supportedSessionKind("file_chunk"),
           "supported session kinds");
    expect(validOutgoingRoute("alice", "alice", "bob", "m1", "chat", 0, 100, 1024),
           "valid outgoing route");
    expect(!validOutgoingRoute("alice", "eve", "bob", "m1", "chat", 0, 100, 1024),
           "reject sender != owner");
    expect(!validOutgoingRoute("alice", "alice", "alice", "m1", "chat", 0, 100, 1024),
           "reject self route");
    expect(!validOutgoingRoute("alice", "alice", "bob", "m1", "chat", -1, 100, 1024),
           "reject negative groupId");
    expect(!validOutgoingRoute("alice", "alice", "bob", "m1", "chat", 0, 100, 1024 * 1024 + 1),
           "reject oversized body");

    const ccfw::WireEnvelope validIn("m1", "bob", "alice", "chat", "YWJj", 0, 10);
    expect(validIncomingRoute(validIn, "alice"), "valid incoming route");
    expect(!validIncomingRoute(validIn, "eve"), "reject incoming wrong owner");
    expect(matchesRoute(validIn, "m1", "bob", "alice", "chat", 0), "matchesRoute true");
    expect(!matchesRoute(validIn, "m1", "bob", "alice", "chat", 1), "matchesRoute group mismatch");

    std::vector<ccfw::WireEnvelope> queue;
    queue.push_back(ccfw::WireEnvelope("a", "alice", "bob", "chat", "AA=="));
    queue.push_back(ccfw::WireEnvelope("b", "alice", "bob", "chat", "AQ=="));
    queue.push_back(ccfw::WireEnvelope("a", "alice", "bob", "chat", "AA=="));
    expect(findMessageId(queue.begin(), queue.end(), "b") == queue.begin() + 1, "findMessageId found");
    expect(findMessageId(queue.begin(), queue.end(), "missing") == queue.end(), "findMessageId end");
    clearQueuedMessage(queue, "a");
    expect(queue.size() == 1 && queue[0].messageId == "b", "clearQueuedMessage removes all matches");

    std::vector<ccfw::ReceivedEnvelope> receipts;
    expect(inspectReplay(receipts, validIn, "sha-1") == NewMessage, "unseen message is NewMessage");
    rememberReceipt(receipts, validIn, "sha-1");
    expect(inspectReplay(receipts, validIn, "sha-1") == SameMessage, "same message is SameMessage");
    expect(inspectReplay(receipts, validIn, "sha-2") == MessageConflict, "different digest conflicts");
    ccfw::WireEnvelope spoof = validIn;
    spoof.from = "mallory";
    expect(inspectReplay(receipts, spoof, "sha-1") == MessageConflict, "different sender conflicts");

    for (int i = 0; i < 4096; ++i) {
        std::ostringstream id;
        id << "hist-" << i;
        rememberReceipt(receipts, ccfw::WireEnvelope(id.str(), "bob", "alice", "chat", "AA=="), "d");
    }
    expect(receipts.size() == 4096, "receipt history capped at 4096");
    expect(inspectReplay(receipts, validIn, "sha-1") == NewMessage, "oldest receipt evicted");

    expect(!currentFileMessage("file_chunk", "fchunk:f1:0", "f1", false), "legacy file_chunk blocked");
    expect(currentFileMessage("file_chunk", "fchunk:f1:0", "f1", true), "current protocol allowed");
    expect(currentFileMessage("chat", "fmeta:f1", "f1", false), "chat kind always allowed");

    std::vector<ccfw::PendingFileChunk> pending;
    pending.push_back(ccfw::PendingFileChunk(
        ccfw::WireEnvelope("fchunk:f1:3", "alice", "bob", "file_chunk", "AA=="), "t3"));
    const ccfw::WireEnvelope lastAck("fack:f1:99", "bob", "alice", "file_ack", "AA==");
    expect(knownFileReceipt(true, "fmeta:f1", "f1", 2, pending, 0), "outgoing metadata receipt");
    expect(knownFileReceipt(true, "fchunk:f1:1", "f1", 2, pending, 0), "outgoing contiguous chunk");
    expect(knownFileReceipt(true, "fchunk:f1:3", "f1", 2, pending, 0), "outgoing pending chunk");
    expect(!knownFileReceipt(true, "fchunk:f1:2", "f1", 2, pending, 0), "unknown chunk not accepted");
    expect(knownFileReceipt(false, "fack:f1:4294967295", "f1", 0, pending, 0), "incoming metadata ack");
    expect(knownFileReceipt(false, "fack:f1:99", "f1", 0, pending, &lastAck), "matches lastAck");

    MockOutboxFlush capFlush(40, 20);
    capFlush.eligibleFlags[0] = false;
    for (int i = 0; i < 20; ++i) {
        std::ostringstream peer;
        peer << "p" << (i % 18);
        capFlush.peers.push_back(peer.str());
    }
    expect(flushOutbox(capFlush), "flushOutbox succeeds with caps");
    expect(capFlush.submitted.size() == 32 && capFlush.submitted[0] == 1,
           "flushOutbox skips ineligible and caps at 32");
    expect(capFlush.retriedPeers.size() == 16, "flushOutbox deduplicates and caps peers at 16");

    MockOutboxFlush failSubmit(3, 1);
    failSubmit.failSubmitAt = 1;
    failSubmit.peers.push_back("bob");
    expect(!flushOutbox(failSubmit) && failSubmit.retriedPeers.empty(),
           "submit failure stops before retries");

    MockOutboxFlush failRetry(1, 2);
    failRetry.peers.push_back("bob");
    failRetry.peers.push_back("carol");
    failRetry.failRetryAt = 1;
    expect(!flushOutbox(failRetry) && failRetry.retriedPeers.size() == 1,
           "retry failure aborts loop");
}

class MockRelaySubmitPort : public RelaySubmitPort {
public:
    MockRelaySubmitPort()
        : exists(true), blocked(false), groupOk(true), persistStatus(QueueOk),
          lastReply(RelayRejected), deliveredCount(0) {}
    bool accountExists(const std::string&) { return exists; }
    bool isBlocked(const RelayRequest&) { return blocked; }
    bool groupAllows(const RelayRequest&) { return groupOk; }
    QueueStatus persist(const RelayRequest&) { return persistStatus; }
    void reply(const RelayRequest&, RelayReply result) { lastReply = result; }
    void deliverOnline(const std::string& recipient) {
        ++deliveredCount;
        deliveredTo = recipient;
    }
    bool exists;
    bool blocked;
    bool groupOk;
    QueueStatus persistStatus;
    RelayReply lastReply;
    int deliveredCount;
    std::string deliveredTo;
};

class MockQueuedDeliveryPort : public QueuedDeliveryPort {
public:
    void deliverStored(const PendingMessage& message) {
        deliveredIds.push_back(message.messageId);
    }
    std::vector<std::string> deliveredIds;
};

class MockRelayAckPort : public RelayAckPort {
public:
    MockRelayAckPort() : hasPending(false), rejectCalled(false), lastError(RelayBadAck) {}
    bool findPending(const std::string& id, PendingMessage& message) {
        if (!hasPending || stored.messageId != id) return false;
        message = stored;
        return true;
    }
    void persistAck(const std::string& id) { steps += "P(" + id + ")"; }
    void rejectAck(RelayAckError result) {
        rejectCalled = true;
        lastError = result;
    }
    void releaseInFlight(const std::string& id) { steps += "R(" + id + ")"; }
    void notifySender(const PendingMessage& message) { steps += "N(" + message.sender + ")"; }
    void continueDelivery() { steps += "C"; }
    bool hasPending;
    PendingMessage stored;
    bool rejectCalled;
    RelayAckError lastError;
    std::string steps;
};

void testRelayFlow() {
    RelayRequest req;
    req.sender = "alice";
    req.recipient = "bob";
    req.messageId = "m1";
    req.kind = "chat";
    req.payload = "YWJj";
    req.groupId = 0;
    req.validGroupField = true;

    {
        MockRelaySubmitPort port;
        RelayRequest selfReq = req;
        selfReq.recipient = "alice";
        submitRelay(selfReq, port);
        expect(port.lastReply == RelayBadTarget && port.deliveredCount == 0, "self relay bad target");
    }
    {
        MockRelaySubmitPort port;
        port.exists = false;
        submitRelay(req, port);
        expect(port.lastReply == RelayNoUser, "unknown recipient");
    }
    {
        MockRelaySubmitPort port;
        RelayRequest badReq = req;
        badReq.payload = "not-base64!";
        submitRelay(badReq, port);
        expect(port.lastReply == RelayBadFields, "invalid base64 payload rejected");
    }
    {
        MockRelaySubmitPort port;
        port.blocked = true;
        submitRelay(req, port);
        expect(port.lastReply == RelayBlocked, "blocked direct message rejected");
    }
    {
        MockRelaySubmitPort port;
        RelayRequest grpFile = req;
        grpFile.groupId = 7;
        grpFile.kind = "file_meta";
        submitRelay(grpFile, port);
        expect(port.lastReply == RelayForbiddenGroup, "non-chat forbidden in group");
    }
    {
        MockRelaySubmitPort port;
        port.persistStatus = QueueAlreadyProcessed;
        submitRelay(req, port);
        expect(port.lastReply == RelayAlreadyReceived && port.deliveredCount == 0,
               "already processed does not redeliver online");
    }
    {
        MockRelaySubmitPort port;
        port.persistStatus = QueueConflict;
        submitRelay(req, port);
        expect(port.lastReply == RelayRejected && port.deliveredCount == 0,
               "conflict maps to RelayRejected");
    }
    {
        MockRelaySubmitPort port;
        submitRelay(req, port);
        expect(port.lastReply == RelayAccepted && port.deliveredCount == 1 && port.deliveredTo == "bob",
               "accepted relay triggers online delivery");
    }

    std::vector<PendingMessage> pending;
    pending.push_back(PendingMessage("bob", "alice", "in-flight", std::string(100, 'a')));
    for (int i = 0; i < 35; ++i) {
        std::ostringstream id;
        id << "q-" << i;
        pending.push_back(PendingMessage("bob", "alice", id.str(), std::string(10, 'b')));
    }
    InFlightMessages inFlight;
    inFlight["in-flight"] = 100;
    MockQueuedDeliveryPort qPort;
    pumpRelayQueue(pending, inFlight, qPort);
    expect(qPort.deliveredIds.size() == 31 && inFlight.size() == 32 && qPort.deliveredIds[0] == "q-0",
           "pumpRelayQueue skips in-flight and respects 32-message limit");

    InFlightMessages byteCapFlight;
    std::vector<PendingMessage> bigPending;
    bigPending.push_back(PendingMessage("bob", "alice", "b1", std::string(5 * 1024 * 1024, 'x')));
    bigPending.push_back(PendingMessage("bob", "alice", "b2", std::string(4 * 1024 * 1024, 'y')));
    MockQueuedDeliveryPort bigPort;
    pumpRelayQueue(bigPending, byteCapFlight, bigPort);
    expect(bigPort.deliveredIds.size() == 1 && bigPort.deliveredIds[0] == "b1",
           "pumpRelayQueue enforces 8 MiB in-flight byte limit");

    {
        MockRelayAckPort ackPort;
        receiveRelayAck("", "alice", ackPort);
        expect(ackPort.rejectCalled && ackPort.lastError == RelayBadAck, "empty id bad ack");
    }
    {
        MockRelayAckPort ackPort;
        receiveRelayAck("missing", "alice", ackPort);
        expect(!ackPort.rejectCalled && ackPort.steps.empty(), "missing pending ack is ignored");
    }
    {
        MockRelayAckPort ackPort;
        ackPort.hasPending = true;
        ackPort.stored = PendingMessage("bob", "alice", "m1", "YWJj");
        receiveRelayAck("m1", "eve", ackPort);
        expect(ackPort.rejectCalled && ackPort.lastError == RelayWrongSender, "wrong sender rejected");
    }
    {
        MockRelayAckPort ackPort;
        ackPort.hasPending = true;
        ackPort.stored = PendingMessage("bob", "alice", "m1", "YWJj");
        receiveRelayAck("m1", "alice", ackPort);
        expect(!ackPort.rejectCalled && ackPort.steps == "P(m1)R(m1)N(alice)C",
               "valid ack persists, releases, notifies sender, and continues delivery");
    }
}

class MockGroupRetry : public GroupRetry {
public:
    explicit MockGroupRetry(std::size_t count) : sentFlags(count, false), failAt(999) {}
    std::size_t size() const { return sentFlags.size(); }
    bool sent(std::size_t index) const { return sentFlags[index]; }
    bool tryAndPersist(std::size_t index) {
        tried.push_back(index);
        if (index == failAt) return false;
        sentFlags[index] = true;
        return true;
    }
    std::vector<bool> sentFlags;
    std::vector<std::size_t> tried;
    std::size_t failAt;
};

void testGroupFlow() {
    std::vector<std::string> requested;
    requested.push_back("bob");
    std::vector<std::string> roster;
    expect(createGroupRoster("Team", requested, "alice", roster) &&
           roster.size() == 2 && roster[0] == "bob" && roster[1] == "alice",
           "createGroupRoster appends creator");
    expect(!createGroupRoster("", requested, "alice", roster), "reject empty group name");
    requested.push_back("bob");
    expect(!createGroupRoster("Team", requested, "alice", roster), "reject duplicate member");

    GroupRow g1;
    g1.id = 10;
    g1.revision = 2;
    g1.name = "Alpha";
    g1.members.push_back("carol");
    g1.members.push_back("alice");
    g1.members.push_back("bob");

    std::vector<GroupRow> stored;
    stored.push_back(g1);
    GroupCatalog catalog;
    expect(restoreGroupCatalog(stored, "alice", catalog) && catalog.size() == 1 &&
           catalog[10].members[0] == "alice" && catalog[10].members[2] == "carol",
           "restoreGroupCatalog sorts members");

    GroupRow dupId = g1;
    stored.push_back(dupId);
    expect(!restoreGroupCatalog(stored, "alice", catalog), "reject duplicate group IDs on restore");

    GroupRow conflict = g1;
    conflict.name = "Beta";
    expect(updateGroupCatalog(catalog, conflict, "alice") == GroupConflict,
           "same revision with different content returns GroupConflict");
    expect(updateGroupCatalog(catalog, g1, "alice") == GroupOutdated,
           "same revision and identical content returns GroupOutdated");
    GroupRow newer = g1;
    newer.revision = 3;
    newer.name = "Alpha v3";
    expect(updateGroupCatalog(catalog, newer, "alice") == GroupChanged &&
           catalog[10].name == "Alpha v3",
           "higher revision updates catalog");

    std::vector<GroupRow> active;
    expect(reconcileGroupCatalog(catalog, active, "alice") && catalog[10].members.empty(),
           "reconcile clears members of inactive group");

    active.push_back(newer);
    expect(reconcileGroupCatalog(catalog, active, "alice") && catalog[10].members.size() == 3,
           "reconcile restores active group");
    std::vector<std::string> recipients;
    expect(planGroupSend(catalog, 10, "hi", "alice", recipients) == GroupReady &&
           recipients.size() == 2 && recipients[0] == "carol" && recipients[1] == "bob",
           "planGroupSend excludes sender");
    expect(planGroupSend(catalog, 10, "hi", "dave", recipients) == GroupForbidden,
           "non-member gets GroupForbidden");
    expect(planGroupSend(catalog, 999, "hi", "alice", recipients) == GroupUnavailable,
           "missing group gets GroupUnavailable");
    expect(planGroupSend(catalog, 10, "", "alice", recipients) == GroupUnavailable,
           "empty text gets GroupUnavailable");

    GroupRow solo;
    solo.id = 20;
    solo.revision = 1;
    solo.name = "Solo";
    solo.members.push_back("alice");
    catalog[20] = solo;
    expect(planGroupSend(catalog, 20, "hi", "alice", recipients) == GroupNoRecipients,
           "single-member group gets GroupNoRecipients");

    MockGroupRetry retry(3);
    retry.sentFlags[0] = true;
    retry.failAt = 1;
    expect(!retryGroup(retry) && retry.tried.size() == 1 && retry.tried[0] == 1,
           "retryGroup skips sent and stops on persistence failure");
    retry.failAt = 999;
    expect(retryGroup(retry) && retry.sentFlags[1] && retry.sentFlags[2],
           "retryGroup completes remaining unsent recipients");
}
} // namespace

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        { "message_flow", testMessageFlow },
        { "binary", testBinary },
        { "message_records", testMessageRecords },
        { "relay_flow", testRelayFlow },
        { "group_flow", testGroupFlow }
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
    std::cout << "Passed " << total << "/" << total << " C++98 core flow test suites.\n";
    return 0;
}
