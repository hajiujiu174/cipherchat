#pragma once
#include <QHash>
#include <QWidget>
#include <QJsonObject>
class QStackedWidget;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QLabel;
class QTimer;
class QPushButton;
class QComboBox;
namespace Messager {
class ChatService;
class ScientificCalculator;
class GomokuPage;
class TankPage;
class BadmintonPage;
class ChessPage;
class CasualGamePage;
struct Message;
class MiniAppsPage final : public QWidget {
    Q_OBJECT
  public:
    // 构造应用目录及便签待办页，初始化后续操作需要的状态或依赖。
    explicit MiniAppsPage(ChatService* service, QWidget* parent = nullptr);
    // 销毁应用目录及便签待办页时释放它持有的资源。
    ~MiniAppsPage() override;
    // 打开应用。
    void openApp(const QString& id);
    // 打开Gomoku邀请。
    void openGomokuInvitation(const Message& message);
  signals:
    // 通知调用方已请求聊天操作。
    void chatRequested(const QString& peer);

  private:
    // 根据服务器、账号及数据目录切换本地应用数据范围，先保存上一范围。
    void updateScope();
    // 加载应用目录及便签待办页。
    void load();
    // 将便签、待办、收藏和最近使用写入当前账号的本机设置。
    void save();
    // 刷新应用目录。
    void refreshCatalog();
    // 更新待办数量。
    void updateTodoCount();
    // 同步值。
    QJsonObject syncValue() const;
    // 把服务器返回的应用数据写入页面控件，供本地保存使用。
    void applySyncValue(const QJsonObject&);
    QString m_syncMode;
    ChatService* m_service;
    QString m_scope;
    QStringList m_favorites, m_recent;
    QStackedWidget* m_stack;
    QLineEdit *m_search, *m_todoInput, *m_expression;
    QPlainTextEdit* m_note;
    QListWidget* m_todos;
    QLabel *m_status, *m_recentLabel, *m_empty, *m_count;
    QTimer* m_saveTimer;
    QHash<QString, QWidget*> m_cards, m_pages;
    QHash<QString, QPushButton*> m_stars;
    QComboBox* m_filter;
    ScientificCalculator* m_calculator;
    GomokuPage* m_gomoku;
    TankPage* m_tank;
    BadmintonPage* m_badminton;
    QHash<QString, GomokuPage*> m_gamePages;
    QHash<QString, ChessPage*> m_chessPages;
    QHash<QString,CasualGamePage*> m_casualPages;
};
} // namespace Messager
