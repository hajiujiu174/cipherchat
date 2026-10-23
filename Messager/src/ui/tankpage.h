#pragma once
#include "services/tankcontroller.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
namespace Messager {
class ChatService;
class TankBoard final:public QWidget {
    Q_OBJECT
public:
    explicit TankBoard(TankController*,QWidget* parent=nullptr);
    QRectF arenaRect() const;
    QPoint pointForArena(const QPointF&) const;
protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void hideEvent(QHideEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    void key(QKeyEvent*,bool);
    bool pointMouse(const QPointF&);
    void stopInput();
    TankController* m_controller;
    QJsonObject m_previous,m_current;
    QElapsedTimer m_blend;
    QTimer m_animation;
    int m_buttons=0;
};
class TankPage final:public QWidget {
    Q_OBJECT
public:
    explicit TankPage(ChatService*,QWidget* parent=nullptr);
    void openWindow();
    void openInvitation(const Message&);
    TankController* controller() const {return m_controller;}
signals:
    void chatRequested(const QString&);
    void returned();
protected:
    void closeEvent(QCloseEvent*) override;
private:
    void toggleFullScreen();
    void refresh();
    void invitations();
    ChatService* m_service;
    TankController* m_controller;
    TankBoard* m_board;
    QComboBox *m_friends,*m_rooms;
    QLabel *m_status,*m_score;
    QPushButton *m_create,*m_join,*m_leave;
    QPushButton* m_fullScreen;
    QVector<Message> m_invites;
};
}
