#pragma once
#include "services/badmintoncontroller.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
namespace Messager {
class ChatService;
class BadmintonBoard final:public QWidget {
    Q_OBJECT
public:
    explicit BadmintonBoard(BadmintonController*,QWidget* parent=nullptr);
    QRectF arenaRect() const;
    QPoint pointForArena(const QPointF&) const;
protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    void key(QKeyEvent*,bool);
    void stopInput();
    BadmintonController* m_controller;
    QJsonObject m_previous,m_current;
    QElapsedTimer m_blend;
    QTimer m_animation;
    int m_buttons=0;
};
class BadmintonPage final:public QWidget {
    Q_OBJECT
public:
    explicit BadmintonPage(ChatService*,QWidget* parent=nullptr);
    void openWindow();
    void openInvitation(const Message&);
    BadmintonController* controller() const {return m_controller;}
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
    BadmintonController* m_controller;
    BadmintonBoard* m_board;
    QComboBox *m_friends,*m_rooms;
    QLabel *m_status,*m_score;
    QPushButton *m_create,*m_join,*m_leave;
    QPushButton* m_fullScreen;
    QVector<Message> m_invites;
};
}
