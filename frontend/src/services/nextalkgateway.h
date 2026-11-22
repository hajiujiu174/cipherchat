#pragma once
#include <QJsonObject>
#include <QObject>
#include <memory>
namespace Messager {
// C++17 header / C++20 implementation. Reuses the actual NEXTALK runtime.
// Constructing does not connect, authenticate, register, or open an account store.
class NextalkGateway final : public QObject {
    Q_OBJECT
  public:
    // 构造桌面命令与事件网关，初始化后续操作需要的状态或依赖。
    explicit NextalkGateway(QObject* parent = nullptr);
    // 销毁桌面命令与事件网关时释放它持有的资源。
    ~NextalkGateway() override;
    // 试解码桌面命令，向界面报告该请求是否符合运行时的参数格式。
    bool validateCommand(const QJsonObject& command) const;
    // 把 Qt JSON 请求解码为运行时命令，保留请求 ID 后送入业务队列。
    bool post(const QJsonObject& command, const QString& requestId = {});
  signals:
    // 返回桌面命令与事件网关使用的事件Received。
    void eventReceived(const QJsonObject& event);
    // 返回桌面命令与事件网关使用的命令已拒绝。
    void commandRejected(const QString& reason);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Messager
