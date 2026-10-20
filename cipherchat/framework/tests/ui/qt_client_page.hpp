#pragma once
#include <QString>
#include <cstdint>
#include <optional>

class QWidget;
namespace ccfw::gui { class MainWindow; }

namespace ccfw::test {
// Test-only adapter: scenarios speak in user actions and observations. Widget
// classes, selectors, translated labels and layout geometry stay in the .cpp.
// Actions must go through real controls; never call the business worker here.
struct FileCardView {
    QString id;
    std::int64_t timestamp = 0;
    int percent = 0, transportPath = -1;
    bool complete = false, canResume = false, canCancel = false;
};
class QtClientPage {
public:
    explicit QtClientPage(gui::MainWindow& window) : window_(window) {}
    void authenticate(const QString& password, bool registerNew);
    void useCodexDesign();
    void useDarkMode();
    bool online() const;
    void openOwnSecurity();
    void logout();
    QString connectionHost() const;
    QString ownFingerprint() const;
    void closeOwnSecurity();
    bool identityConfirmationPending() const;
    void openPeerSecurity();
    QString peerFingerprint() const;
    void confirmIdentity();
    void selectConversation(const QString& peer);
    void sendMessage(const QString& text);
    bool hasMessage(const QString& text) const;
    bool hasReceipt(const QString& text) const;
    void showAttachments(bool visible);
    bool attachmentsVisible() const;
    void selectFile(const QString& path);
    QString selectedFile() const;
    QString selectedFileRecipient() const;
    bool canSendFile() const;
    void sendSelectedFile();
    std::optional<FileCardView> fileCard() const;
    bool timelineOrder(const QString& before, const QString& after) const;
    void showGroups();
    void createGroup(const QString& name, const QString& members);
    bool hasGroup() const;
    void sendGroupMessage(const QString& text);
    bool directFilesEnabled() const;
    void setDirectFilesEnabled(bool enabled);
    QString notification() const;
    QWidget& ownSecuritySurface() const;
    QWidget& peerSecuritySurface() const;

    // Optional presentation policy checks, called only by the layout suite.
    void checkAccountLayout() const;
    void checkAttachmentsCollapsed() const;
    void checkPeerDetailsInitiallyHidden() const;
    void scrollFileIntoView();
    void checkFileFitsViewport() const;
private:
    gui::MainWindow& window_;
};
}
