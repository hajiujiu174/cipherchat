#pragma once
#include "business_worker.hpp"
#include <QDialog>
#include <functional>

namespace ccfw::gui {
class FeatureUi {
public:
    // 构造客户端界面，初始化后续操作需要的状态或依赖。
    explicit FeatureUi(QWidget* owner, std::function<bool(Command)> post) : owner(owner), post(std::move(post)) {}
    // 取出并处理客户端界面。
    void consume(const WorkerEvent&);
    // 返回客户端界面使用的消息详情。
    void messageDetails(const QString& id = {});
    // 返回客户端界面使用的联系人卡片。
    QDialog* contactCard(const std::string& username, bool own = false);
    // 返回客户端界面使用的投递队列。
    void deliveries(const QString& peer);
    AccountScope scope;
    WorkspaceState workspace;
    std::vector<DeliveryBundle> bundles;
    std::vector<MessageStageEvent> stages;
    bool tlsValidated = false;
    bool uidLookupSupported = false;
    std::map<std::string, std::int64_t> uids;
    std::function<QString(const std::string&)> readRemark;
    std::function<bool(const std::string&, const QString&)> saveRemark;
private:
    QWidget* owner;
    std::function<bool(Command)> post;
};
}
