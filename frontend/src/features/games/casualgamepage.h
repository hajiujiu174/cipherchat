#pragma once
#include "features/games/casualgames.h"
#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>
class QLabel;class QPushButton;class QComboBox;
class QPainter;
namespace Messager {
class CasualBoard final:public QWidget {
    Q_OBJECT
public:
    // 构造游戏联机，初始化后续操作需要的状态或依赖。
    explicit CasualBoard(const QString& game,QWidget* parent=nullptr);
    // 重置当前小游戏并重新开始一局。
    void restart(int level=1);/* 取消输入。 */ void cancelInput();
    // 撤销游戏联机。
    bool undo();/* 洗牌游戏联机。 */ bool shuffle();/* 移动To暂存区。 */ bool moveToReserve();
    // 返回游戏联机使用的跳跃游戏。
    const JumpGame& jumpGame()const{return m_jump;}
    // 返回游戏联机使用的羊了个羊游戏。
    const SheepGame& sheepGame()const{return m_sheep;}
    // 返回游戏联机使用的屏幕坐标。
    QPoint screenPoint(const QPointF&)const;
    // 报告跳跃操作是否正在蓄力。
    bool charging()const{return m_charging;}
signals:
    // 通知订阅方组件状态已经变化。
    void changed();
protected:
    // 在 Qt 的绘制回调中渲染游戏联机的当前外观。
    void paintEvent(QPaintEvent*)override;
    // 响应游戏联机的鼠标按下，同步界面或输入状态。
    void mousePressEvent(QMouseEvent*)override;/* 响应游戏联机的鼠标松开，同步界面或输入状态。 */ void mouseReleaseEvent(QMouseEvent*)override;
    // 响应游戏联机的按键按下，同步界面或输入状态。
    void keyPressEvent(QKeyEvent*)override;/* 响应游戏联机的按键松开，同步界面或输入状态。 */ void keyReleaseEvent(QKeyEvent*)override;
    // 响应游戏联机的焦点移出，同步界面或输入状态。
    void focusOutEvent(QFocusEvent*)override;/* 响应游戏联机的隐藏，同步界面或输入状态。 */ void hideEvent(QHideEvent*)override;/* 响应游戏联机的显示，同步界面或输入状态。 */ void showEvent(QShowEvent*)override;
private:
    // 处理游戏画布的按下动作，并记录后续交互需要的输入状态。
    void press();/* 释放游戏联机。 */ void release();/* 绘制跳跃。 */ void drawJump(QPainter&);/* 绘制羊了个羊。 */ void drawSheep(QPainter&);
    // 返回游戏联机使用的logical坐标点。
    QPointF logicalPoint(const QPointF&)const;/* 计算游戏画布到当前窗口的缩放比例。 */ double scale()const;/* 计算游戏画布在窗口中的绘制原点。 */ QPointF origin()const;
    QString m_game;JumpGame m_jump,m_before;SheepGame m_sheep;
    QTimer m_timer;QElapsedTimer m_hold,m_animation;bool m_charging=false,m_flying=false;
};
class CasualGamePage final:public QWidget {
    Q_OBJECT
public:
    // 构造游戏联机，初始化后续操作需要的状态或依赖。
    explicit CasualGamePage(const QString& game,QWidget* parent=nullptr);
    // 打开窗口。
    void openWindow();/* 设置游戏联机使用的账号范围。 */ void setScope(const QString& scope);
    // 返回本机保存的最佳游戏成绩。
    int bestScore()const{return m_best;}
signals:
    // 通知订阅方游戏联机已发生返回操作。
    void returned();
protected:
    // 响应游戏联机的窗口关闭，同步界面或输入状态。
    void closeEvent(QCloseEvent*)override;
private:
    // 刷新游戏联机。
    void refresh();/* 切换全屏。 */ void toggleFullScreen();
    QString m_game,m_scope;CasualBoard* m_board;QLabel* m_status;
    QPushButton *m_full,*m_undo=nullptr,*m_shuffle=nullptr,*m_reserve=nullptr,*m_next=nullptr;
    QComboBox* m_level=nullptr;int m_best=0,m_wins=0;bool m_wasWon=false;
};
}
