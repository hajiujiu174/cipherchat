#pragma once
#include "components.h"
#include "controllers/appcontroller.h"
#include "models/conversationmodel.h"
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
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;
    AccountDialog* accountDialog() const;
    void showPage(int index);
    void showLogin();
    void setDetailsVisible(bool visible);
    bool detailsVisible() const;
    void syncSelection();
    void showAccountDialog();
    void showProfile(const QString& user);
  signals:
    void loginOpened(QWidget* dialog);

  protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

  private:
    void bindProfileAvatar(Avatar*, const QString& user);
    QWidget* buildRail();
    QWidget* buildChats();
    QWidget* buildContacts();
    QWidget* buildFiles();
    QWidget* buildNotifications();
    QWidget* buildSettings();
    QWidget* buildLiveContacts();
    QWidget* buildLiveNotifications();
    void refreshLiveContacts();
    void refreshLiveActivities();
    void refreshSession();
    void showCreateGroup();
    void buildDetails();
    void renderMessages(bool forceBottom = false);
    void refreshTransfers();
    void locateTransfer(const QString& id);
    void chooseFile();
    void submit();
    void refreshDetails();
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
