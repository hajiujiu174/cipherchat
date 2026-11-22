#pragma once
#include "ccfw/chat_service.hpp"
#include "ccfw/file_transfer.hpp"

namespace ccfw {
// Single business-thread coordinator; the existing file service still owns all
// encryption, checkpointing, direct/relay transport and byte acknowledgements.
class DeliveryService {
public:
    // 构造批量交付服务，初始化后续操作需要的状态或依赖。
    DeliveryService(ILocalStore& store, ChatService& chat, FileTransferService& files) : store_(store), chat_(chat), files_(files) {}
    // 创建带稳定标识的文件交付清单，校验版本关联及每个源文件并持久化。
    Result<std::string> create(const CreateDeliveryBundle&);
    // 为持久化交付条目启动缺失的文件任务，并跟踪批量交付状态。
    Result<> tick();
    // 继续批量交付服务。
    Result<> resume(const ResumeDeliveryItem&);
    // 取消批量交付服务。
    Result<> cancel(const CancelDeliveryBundle&);
    // 在收件端逐项核对交付文件与清单摘要，通过后发送人工确认。
    Result<> confirm(const ConfirmDelivery&);
    // 取出事件。
    std::vector<DeliveryBundle> takeEvents();
private:
    ILocalStore& store_;
    ChatService& chat_;
    FileTransferService& files_;
    std::map<std::string, std::string> published_;
    std::vector<DeliveryBundle> events_;
};
// 返回delivery_service 模块使用的投递进度。
double deliveryProgress(const DeliveryBundle&);
}
