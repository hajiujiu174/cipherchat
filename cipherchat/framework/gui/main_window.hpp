#pragma once
#include <QMainWindow>
#include "messenger.hpp"
#include "ccfw/types.hpp"
#include "ccfw/transport.hpp"
#include <functional>
#include <memory>
#include <QString>
#include <QVariantMap>
namespace ccfw::gui {
struct WindowOptions {
    ConnectionOptions connection;
    std::filesystem::path dataDirectory;
    std::string username;
    std::optional<bool> directEnabled; // nullopt loads the saved preference.
    std::string accountServerId; // Explicit identity-preserving endpoint migration.
    std::string frontend; // Optional launch-only design; saved preference remains untouched.
    std::function<std::unique_ptr<ITransport>()> transportFactory;
    std::function<bool(const QString&)> localFileOpener, localFileRevealer; // Optional GUI test adapters; defaults use the OS.
};
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(WindowOptions options = {});
    ~MainWindow() override;
    QString switchFrontend(const QString& design);
    void navigate(MessengerPage page);
    void openConversation(ConversationTarget target);
protected:
    void closeEvent(QCloseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
