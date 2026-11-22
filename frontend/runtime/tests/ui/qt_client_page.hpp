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
    // 构造桌面控件，初始化后续操作需要的状态或依赖。
    explicit QtClientPage(gui::MainWindow& window) : window_(window) {}
    // 认证桌面控件。
    void authenticate(const QString& password, bool registerNew);
    // 返回桌面控件使用的useCodex设计样式。
    void useCodexDesign();
    // 返回桌面控件使用的use深色模式。
    void useDarkMode();
    // 判断桌面控件的是否已在线。
    bool online() const;
    // 打开OwnSecurity。
    void openOwnSecurity();
    // 返回桌面控件使用的退出登录。
    void logout();
    // 返回桌面控件使用的连接主机。
    QString connectionHost() const;
    // 获取桌面控件对应的 ownFingerprint，供调用方读取或参与计算。
    QString ownFingerprint() const;
    // 关闭OwnSecurity。
    void closeOwnSecurity();
    // 返回桌面控件使用的身份Confirmation待处理。
    bool identityConfirmationPending() const;
    // 打开对端Security。
    void openPeerSecurity();
    // 返回桌面控件使用的对端Fingerprint。
    QString peerFingerprint() const;
    // 确认身份。
    void confirmIdentity();
    // 选择会话。
    void selectConversation(const QString& peer);
    // 发送消息。
    void sendMessage(const QString& text);
    // 检查桌面控件是否具有消息。
    bool hasMessage(const QString& text) const;
    // 检查桌面控件是否具有签收。
    bool hasReceipt(const QString& text) const;
    // 显示Attachments。
    void showAttachments(bool visible);
    // 获取桌面控件对应的 attachmentsVisible，供调用方读取或参与计算。
    bool attachmentsVisible() const;
    // 选择文件。
    void selectFile(const QString& path);
    // 返回桌面控件使用的选中的文件。
    QString selectedFile() const;
    // 返回桌面控件使用的选中的文件收件人。
    QString selectedFileRecipient() const;
    // 判断桌面控件当前是否允许Send文件。
    bool canSendFile() const;
    // 发送选中的文件。
    void sendSelectedFile();
    // 返回桌面控件使用的文件卡片。
    std::optional<FileCardView> fileCard() const;
    // 获取桌面控件对应的 timelineOrder，供调用方读取或参与计算。
    bool timelineOrder(const QString& before, const QString& after) const;
    // 显示群聊。
    void showGroups();
    // 创建群聊。
    void createGroup(const QString& name, const QString& members);
    // 检查桌面控件是否具有群聊。
    bool hasGroup() const;
    // 发送群消息。
    void sendGroupMessage(const QString& text);
    // 返回桌面控件使用的通知。
    QString notification() const;
    // 获取桌面控件对应的 ownSecuritySurface，供调用方读取或参与计算。
    QWidget& ownSecuritySurface() const;
    // 返回桌面控件使用的对端SecuritySurface。
    QWidget& peerSecuritySurface() const;

    // Optional presentation policy checks, called only by the layout suite.
    // 检查账号布局，交给调用方区分有效结果与错误。
    void checkAccountLayout() const;
    // 检查AttachmentsCollapsed，交给调用方区分有效结果与错误。
    void checkAttachmentsCollapsed() const;
    // 检查对端详情InitiallyHidden，交给调用方区分有效结果与错误。
    void checkPeerDetailsInitiallyHidden() const;
    // 返回桌面控件使用的滚动文件Into视图。
    void scrollFileIntoView();
    // 检查文件FitsViewport，交给调用方区分有效结果与错误。
    void checkFileFitsViewport() const;
private:
    gui::MainWindow& window_;
};
}
