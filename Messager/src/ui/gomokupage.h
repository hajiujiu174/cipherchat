#pragma once
#include "services/gomokucontroller.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
namespace Messager {
class GomokuBoard final : public QWidget {
    Q_OBJECT
  public:
    explicit GomokuBoard(QWidget* parent = nullptr);
    void setRoom(const GomokuRoom& room, bool canMove);
    QPoint cellCenter(int x, int y) const;
    QSize sizeHint() const override {
        return {420, 420};
    }
  signals:
    void cellClicked(int x, int y);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

  private:
    QRectF gridRect() const;
    GomokuRoom m_room;
    bool m_canMove = false;
};
class GomokuPage final : public QWidget {
    Q_OBJECT
  public:
    explicit GomokuPage(ChatService* service, QWidget* parent = nullptr);
    GomokuController* controller() const {
        return m_game;
    }
    void openInvitation(const Message& message);
  signals:
    void activated();
    void chatRequested(const QString& peer);

  private:
    void refresh();
    void refreshFriends();
    ChatService* m_service;
    GomokuController* m_game;
    GomokuBoard* m_board;
    QComboBox *m_friends, *m_rooms;
    QLabel *m_status, *m_players, *m_error;
    QPushButton *m_create, *m_join, *m_leave, *m_sync, *m_chat;
    QString m_selected;
};
} // namespace Messager
