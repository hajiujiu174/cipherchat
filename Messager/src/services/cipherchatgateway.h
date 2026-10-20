#pragma once
#include <QJsonObject>
#include <QObject>
#include <memory>
namespace Messager {
// C++17 header / C++20 implementation. Reuses the actual CipherChat runtime.
// Constructing does not connect, authenticate, register, or open an account store.
class CipherChatGateway final : public QObject {
    Q_OBJECT
  public:
    explicit CipherChatGateway(QObject* parent = nullptr);
    ~CipherChatGateway() override;
    bool validateCommand(const QJsonObject& command) const;
    bool post(const QJsonObject& command, const QString& requestId = {});
  signals:
    void eventReceived(const QJsonObject& event);
    void commandRejected(const QString& reason);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Messager
