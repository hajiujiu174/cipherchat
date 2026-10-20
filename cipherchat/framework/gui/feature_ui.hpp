#pragma once
#include "business_worker.hpp"
#include <QDialog>
#include <functional>

namespace ccfw::gui {
class FeatureUi {
public:
    explicit FeatureUi(QWidget* owner, std::function<bool(Command)> post) : owner(owner), post(std::move(post)) {}
    void consume(const WorkerEvent&);
    void messageDetails(const QString& id = {});
    QDialog* identityCard(const PeerIdentity&, bool own = false);
    void deliveries(const QString& peer);
    AccountScope scope;
    PeerIdentity ownIdentity;
    std::map<std::string, PeerIdentity> identities;
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
