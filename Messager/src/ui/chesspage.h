#pragma once
#include "services/chesscontroller.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
namespace Messager {
class ChessBoard final : public QWidget {
    Q_OBJECT
  public:
    explicit ChessBoard(QWidget* parent = nullptr, const QString& game = "chess");
    void setRoom(const ChessRoom& room,bool canMove,bool flipped=false);
    QPoint cellCenter(int x, int y) const;
    QSize sizeHint() const override {
        return {420, 420};
    }
  signals:
    void moveChosen(const QString& code);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

  private:
    QRectF gridRect() const;
    ChessRoom m_room;
    bool m_canMove=false,m_flipped=false;
    int m_selected=-1;
    QStringList m_legal;
};
class ChessPage final : public QWidget {
    Q_OBJECT
  public:
    explicit ChessPage(ChatService* service, QWidget* parent = nullptr, const QString& game = "chess");
    ChessController* controller() const {
        return m_game;
    }
    void openInvitation(const Message& message);
    void openWindow();
  signals:
    void activated();
    void returned();
    void chatRequested(const QString& peer);

  protected:
    void closeEvent(QCloseEvent*) override;
  private:
    void toggleFullScreen();
    void refresh();
    void refreshFriends();
    ChatService* m_service;
    ChessController* m_game;
    ChessBoard* m_board;
    QComboBox *m_friends, *m_rooms;
    QLabel *m_status, *m_players, *m_error;
    QPushButton *m_create, *m_join, *m_leave, *m_sync, *m_chat;
    QString m_selected, m_gameId;
    QPushButton* m_fullScreen;
};
} // namespace Messager
