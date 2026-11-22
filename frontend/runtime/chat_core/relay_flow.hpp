// 人工实现，由 ChatGPT 规范化。

#ifndef NEXTALK_CHAT_RELAY_FLOW_HPP
#define NEXTALK_CHAT_RELAY_FLOW_HPP

#include "relay_store.hpp"
#include <map>
#include <stdint.h>

namespace nextalk {
namespace chat {

struct RelayRequest {
    std::string sender;
    std::string recipient;
    std::string messageId;
    std::string kind;
    std::string payload;
    int64_t groupId;
    bool validGroupField;
};

enum RelayReply {
    RelayBadTarget, RelayNoUser, RelayBadFields, RelayBlocked, RelayForbiddenGroup,
    RelayRejected, RelayAlreadyReceived, RelayAccepted
};

class RelaySubmitPort {
public:
    virtual ~RelaySubmitPort() {}
    virtual bool accountExists(const std::string& user) = 0;
    virtual bool isBlocked(const RelayRequest& request) = 0;
    virtual bool groupAllows(const RelayRequest& request) = 0;
    virtual QueueStatus persist(const RelayRequest& request) = 0;
    virtual void reply(const RelayRequest& request, RelayReply result) = 0;
    virtual void deliverOnline(const std::string& recipient) = 0;
};

void submitRelay(const RelayRequest& request, RelaySubmitPort& port);

typedef std::map<std::string, std::size_t> InFlightMessages;

class QueuedDeliveryPort {
public:
    virtual ~QueuedDeliveryPort() {}
    virtual void deliverStored(const PendingMessage& message) = 0;
};

void pumpRelayQueue(const std::vector<PendingMessage>& pending,
                    InFlightMessages& inFlight, QueuedDeliveryPort& port);

enum RelayAckError { RelayBadAck, RelayWrongSender };

class RelayAckPort {
public:
    virtual ~RelayAckPort() {}
    virtual bool findPending(const std::string& id, PendingMessage& message) = 0;
    virtual void persistAck(const std::string& id) = 0;
    virtual void rejectAck(RelayAckError result) = 0;
    virtual void releaseInFlight(const std::string& id) = 0;
    virtual void notifySender(const PendingMessage& message) = 0;
    virtual void continueDelivery() = 0;
};

void receiveRelayAck(const std::string& id, const std::string& sender, RelayAckPort& port);

} // namespace chat
} // namespace nextalk

#endif
