#pragma once
#include "features/games/tankcontroller.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
namespace Messager {
class ChatService;
class TankBoard final:public QWidget {
    Q_OBJECT
public:
    // 构造游戏联机，初始化后续操作需要的状态或依赖。
    explicit TankBoard(TankController*,QWidget* parent=nullptr);
    // 返回游戏联机使用的场地矩形。
    QRectF arenaRect() const;
    // 返回游戏联机使用的坐标点For场地。
    QPoint pointForArena(const QPointF&) const;
protected:
    // 在 Qt 的绘制回调中渲染游戏联机的当前外观。
    void paintEvent(QPaintEvent*) override;
    // 响应游戏联机的按键按下，同步界面或输入状态。
    void keyPressEvent(QKeyEvent*) override;
    // 响应游戏联机的按键松开，同步界面或输入状态。
    void keyReleaseEvent(QKeyEvent*) override;
    // 响应游戏联机的焦点移出，同步界面或输入状态。
    void focusOutEvent(QFocusEvent*) override;
    // 响应游戏联机的隐藏，同步界面或输入状态。
    void hideEvent(QHideEvent*) override;
    // 响应游戏联机的鼠标按下，同步界面或输入状态。
    void mousePressEvent(QMouseEvent*) override;
    // 响应游戏联机的鼠标移动，同步界面或输入状态。
    void mouseMoveEvent(QMouseEvent*) override;
    // 响应游戏联机的鼠标松开，同步界面或输入状态。
    void mouseReleaseEvent(QMouseEvent*) override;
    // 响应游戏联机的指针离开，同步界面或输入状态。
    void leaveEvent(QEvent*) override;
private:
    // 返回游戏联机使用的键。
    void key(QKeyEvent*,bool);
    // 返回游戏联机使用的坐标点鼠标。
    bool pointMouse(const QPointF&);
    // 停止输入。
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
    // 构造游戏联机，初始化后续操作需要的状态或依赖。
    explicit TankPage(ChatService*,QWidget* parent=nullptr);
    // 打开窗口。
    void openWindow();
    // 打开邀请。
    void openInvitation(const Message&);
    // 返回游戏联机的页面使用的控制器。
    TankController* controller() const {return m_controller;}
signals:
    // 通知调用方已请求聊天操作。
    void chatRequested(const QString&);
    // 通知订阅方游戏联机已发生返回操作。
    void returned();
protected:
    // 响应游戏联机的窗口关闭，同步界面或输入状态。
    void closeEvent(QCloseEvent*) override;
private:
    // 切换全屏。
    void toggleFullScreen();
    // 刷新游戏联机。
    void refresh();
    // 返回游戏联机使用的邀请列表。
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
