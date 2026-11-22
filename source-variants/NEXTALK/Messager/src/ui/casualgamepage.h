#pragma once
#include "services/casualgames.h"
#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>
class QLabel;class QPushButton;class QComboBox;
class QPainter;
namespace Messager {
class CasualBoard final:public QWidget {
    Q_OBJECT
public:
    explicit CasualBoard(const QString& game,QWidget* parent=nullptr);
    void restart(int level=1);void cancelInput();
    bool undo();bool shuffle();bool moveToReserve();
    const JumpGame& jumpGame()const{return m_jump;}
    const SheepGame& sheepGame()const{return m_sheep;}
    QPoint screenPoint(const QPointF&)const;
    bool charging()const{return m_charging;}
signals:
    void changed();
protected:
    void paintEvent(QPaintEvent*)override;
    void mousePressEvent(QMouseEvent*)override;void mouseReleaseEvent(QMouseEvent*)override;
    void keyPressEvent(QKeyEvent*)override;void keyReleaseEvent(QKeyEvent*)override;
    void focusOutEvent(QFocusEvent*)override;void hideEvent(QHideEvent*)override;void showEvent(QShowEvent*)override;
private:
    void press();void release();void drawJump(QPainter&);void drawSheep(QPainter&);
    QPointF logicalPoint(const QPointF&)const;double scale()const;QPointF origin()const;
    QString m_game;JumpGame m_jump,m_before;SheepGame m_sheep;
    QTimer m_timer;QElapsedTimer m_hold,m_animation;bool m_charging=false,m_flying=false;
};
class CasualGamePage final:public QWidget {
    Q_OBJECT
public:
    explicit CasualGamePage(const QString& game,QWidget* parent=nullptr);
    void openWindow();void setScope(const QString& scope);
    int bestScore()const{return m_best;}
signals:
    void returned();
protected:
    void closeEvent(QCloseEvent*)override;
private:
    void refresh();void toggleFullScreen();
    QString m_game,m_scope;CasualBoard* m_board;QLabel* m_status;
    QPushButton *m_full,*m_undo=nullptr,*m_shuffle=nullptr,*m_reserve=nullptr,*m_next=nullptr;
    QComboBox* m_level=nullptr;int m_best=0,m_wins=0;bool m_wasWon=false;
};
}
