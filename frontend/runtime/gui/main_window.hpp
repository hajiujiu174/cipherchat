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
    std::string accountServerId; // Explicit identity-preserving endpoint migration.
    std::string frontend; // Optional launch-only design; saved preference remains untouched.
    std::function<std::unique_ptr<ITransport>()> transportFactory;
    std::function<bool(const QString&)> localFileOpener, localFileRevealer; // Optional GUI test adapters; defaults use the OS.
};
class MainWindow final : public QMainWindow {
public:
    // 构造桌面主窗口，初始化后续操作需要的状态或依赖。
    explicit MainWindow(WindowOptions options = {});
    // 销毁桌面主窗口时释放它持有的资源。
    ~MainWindow() override;
    // 切换前端。
    QString switchFrontend(const QString& design);
    // 跳转到桌面主窗口。
    void navigate(MessengerPage page);
    // 打开会话。
    void openConversation(ConversationTarget target);
protected:
    // 响应桌面主窗口的窗口关闭，同步界面或输入状态。
    void closeEvent(QCloseEvent*) override;
    // 响应桌面主窗口的尺寸变化，同步界面或输入状态。
    void resizeEvent(QResizeEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
