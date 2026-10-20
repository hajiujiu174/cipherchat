#pragma once
#include <QHash>
#include <QWidget>
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
struct Message;
class MiniAppsPage final : public QWidget {
    Q_OBJECT
  public:
    explicit MiniAppsPage(ChatService* service, QWidget* parent = nullptr);
    ~MiniAppsPage() override;
    void openApp(const QString& id);
    void openGomokuInvitation(const Message& message);
  signals:
    void chatRequested(const QString& peer);

  private:
    void updateScope();
    void load();
    void save();
    void refreshCatalog();
    void updateTodoCount();
    ChatService* m_service;
    QString m_scope;
    QStringList m_favorites, m_recent;
    QStackedWidget* m_stack;
    QLineEdit *m_search, *m_todoInput, *m_expression, *m_webAddress;
    QPlainTextEdit* m_note;
    QListWidget* m_todos;
    QLabel *m_status, *m_recentLabel, *m_empty, *m_count, *m_webStatus;
    QTimer* m_saveTimer;
    QHash<QString, QWidget*> m_cards;
    QHash<QString, QPushButton*> m_stars;
    QComboBox* m_filter;
    ScientificCalculator* m_calculator;
    GomokuPage* m_gomoku;
};
} // namespace Messager
