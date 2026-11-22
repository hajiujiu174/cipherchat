#pragma once
#include "components.h"
#include "controllers/appcontroller.h"
#include "features/chat/conversationmodel.h"
#include <QHash>
#include <QMainWindow>
#include <QPointer>
class QStackedWidget;
class QListView;
class QLineEdit;
class QScrollArea;
class QSplitter;
class QVBoxLayout;
class QComboBox;
class QTimer;
namespace Messager {
class AccountDialog;
class ProfilePage;
class MomentsPage;
class MiniAppsPage;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    // 构造桌面主窗口，初始化后续操作需要的状态或依赖。
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);
    // 销毁桌面主窗口时释放它持有的资源。
    ~MainWindow() override;
    // 返回桌面主窗口使用的账号对话框。
    AccountDialog* accountDialog() const;
    // 显示Page。
    void showPage(int index);
    // 显示登录。
    void showLogin();
    // 设置桌面主窗口使用的详情Visible。
    void setDetailsVisible(bool visible);
    // 返回桌面主窗口的详情面板是否可见。
    bool detailsVisible() const;
    // 切换当前会话，暂存离开的会话草稿，并恢复新会话的输入框与详情。
    void syncSelection();
    // 打开或复用账号窗口，使用户能登录、注册或调整连接配置。
    void showAccountDialog();
    // 显示资料。
    void showProfile(const QString& user);
  signals:
    // 返回桌面主窗口使用的登录已打开。
    void loginOpened(QWidget* dialog);

  protected:
    // 响应桌面主窗口的尺寸变化，同步界面或输入状态。
    void resizeEvent(QResizeEvent*) override;
    // 响应桌面主窗口的事件过滤，同步界面或输入状态。
    bool eventFilter(QObject*, QEvent*) override;

  private:
    // 绑定资料头像。
    void bindProfileAvatar(Avatar*, const QString& user);
    // 构建Rail。
    QWidget* buildRail();
    // 构建Chats。
    QWidget* buildChats();
    // 构建联系人。
    QWidget* buildContacts();
    // 构建文件。
    QWidget* buildFiles();
    // 构建通知。
    QWidget* buildNotifications();
    // 构建设置。
    QWidget* buildSettings();
    // 构建联机联系人。
    QWidget* buildLiveContacts();
    // 构建联机通知。
    QWidget* buildLiveNotifications();
    // 刷新联机联系人。
    void refreshLiveContacts();
    // 刷新联机活动通知。
    void refreshLiveActivities();
    // 按登录、在线和会话状态刷新桌面提示及按钮可用性。
    void refreshSession();
    // 显示Create群聊。
    void showCreateGroup();
    // 构建详情。
    void buildDetails();
    // 按当前会话、搜索条件和可视宽度重建聊天气泡，保留阅读位置或跟随新消息滚动。
    void renderMessages(bool forceBottom = false);
    // 用服务层的传输状态更新文件卡片，连接打开、暂停、继续和定位操作。
    void refreshTransfers();
    // 用文件任务关联的会话 ID 和消息 ID 跳转，滚动到对应气泡并短暂高亮。
    void locateTransfer(const QString& id);
    // 打开系统文件选择器，将用户确认的路径交给当前会话。
    void chooseFile();
    // 读取输入框正文，把它提交给当前会话，提交成功后清空草稿并刷新消息区。
    void submit();
    // 刷新详情。
    void refreshDetails();
    // 同步列表选中项。
    void syncListSelection();
    ProfilePage* m_profilePage=nullptr;
    MomentsPage* m_momentsPage=nullptr;
    MiniAppsPage* m_miniApps=nullptr;
    int m_profileReturnPage=0;
    AppController* m_controller;
    QPointer<AccountDialog> m_accountDialog;
    ConversationModel* m_model;
    ConversationFilter* m_filter;
    QStackedWidget* m_pages = nullptr;
    QVector<QPushButton*> m_nav;
    QListView* m_conversations = nullptr;
    QLineEdit *m_search = nullptr, *m_messageSearch = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_chat = nullptr;
    QScrollArea* m_scroll = nullptr;
    MessageEditor* m_editor = nullptr;
    QVBoxLayout *m_headerText = nullptr, *m_detailBody = nullptr;
    QFrame* m_details = nullptr;
    QWidget* m_headerAvatar = nullptr;
    QLabel *m_chatTitle = nullptr, *m_chatSubtitle = nullptr, *m_emptyList = nullptr,
           *m_transferSummary = nullptr;
    QVBoxLayout* m_transferLayout = nullptr;
    QHash<QString, TransferCard*> m_transferCards;
    QComboBox* m_transferFilter = nullptr;
    QString m_transferConversation;
    QPushButton* m_send = nullptr;
    QHash<QString, QString> m_drafts;
    QString m_displayedId;
    QByteArray m_messageRenderKey;
    QString m_highlightedMessageId;
    quint64 m_locationGeneration = 0;
    bool m_failNext = false;
    int m_filterMode = 0;
    QTimer* m_reflowTimer = nullptr;
    bool m_followTail = true;
    QLabel *m_sessionNotice = nullptr, *m_railStatus = nullptr;
    QVBoxLayout *m_liveContacts = nullptr, *m_liveRequests = nullptr, *m_liveResults = nullptr,
                *m_liveActivities = nullptr;
    QLineEdit* m_liveSearch = nullptr;
    QPushButton* m_searchMore = nullptr;
};
} // namespace Messager
