#pragma once
#include "ccfw/chat_service.hpp"
#include "ccfw/file_transfer.hpp"

namespace ccfw {
// Single business-thread coordinator; the existing file service still owns all
// encryption, checkpointing, direct/relay transport and byte acknowledgements.
class DeliveryService {
public:
    DeliveryService(ILocalStore& store, ChatService& chat, FileTransferService& files) : store_(store), chat_(chat), files_(files) {}
    Result<std::string> create(const CreateDeliveryBundle&);
    Result<> tick();
    Result<> resume(const ResumeDeliveryItem&);
    Result<> cancel(const CancelDeliveryBundle&);
    Result<> confirm(const ConfirmDelivery&);
    std::vector<DeliveryBundle> takeEvents();
private:
    ILocalStore& store_;
    ChatService& chat_;
    FileTransferService& files_;
    std::map<std::string, std::string> published_;
    std::vector<DeliveryBundle> events_;
};
double deliveryProgress(const DeliveryBundle&);
}
