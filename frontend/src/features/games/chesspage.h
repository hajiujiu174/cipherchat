#pragma once
#include "features/games/chesscontroller.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
namespace Messager {
class ChessBoard final : public QWidget {
    Q_OBJECT
  public:
    // 构造游戏联机，初始化后续操作需要的状态或依赖。
    explicit ChessBoard(QWidget* parent = nullptr, const QString& game = "chess");
    // 设置游戏联机使用的房间。
    void setRoom(const ChessRoom& room,bool canMove,bool flipped=false);
    // 返回游戏联机使用的格子中心位置。
    QPoint cellCenter(int x, int y) const;
    // 返回游戏联机的控件建议尺寸。
    QSize sizeHint() const override {
        return {420, 420};
    }
  signals:
    // 移动Chosen。
    void moveChosen(const QString& code);

  protected:
    // 在 Qt 的绘制回调中渲染游戏联机的当前外观。
    void paintEvent(QPaintEvent*) override;
    // 响应游戏联机的鼠标按下，同步界面或输入状态。
    void mousePressEvent(QMouseEvent*) override;

  private:
    // 返回游戏联机使用的棋盘区域。
    QRectF gridRect() const;
    ChessRoom m_room;
    bool m_canMove=false,m_flipped=false;
    int m_selected=-1;
    QStringList m_legal;
};
class ChessPage final : public QWidget {
    Q_OBJECT
  public:
    // 构造游戏联机，初始化后续操作需要的状态或依赖。
    explicit ChessPage(ChatService* service, QWidget* parent = nullptr, const QString& game = "chess");
    // 返回游戏联机的页面使用的控制器。
    ChessController* controller() const {
        return m_game;
    }
    // 打开邀请。
    void openInvitation(const Message& message);
    // 打开窗口。
    void openWindow();
  signals:
    // 通知控制器指定会话、房间或界面条目被激活。
    void activated();
    // 通知订阅方游戏联机已发生返回操作。
    void returned();
    // 通知调用方已请求聊天操作。
    void chatRequested(const QString& peer);

  protected:
    // 响应游戏联机的窗口关闭，同步界面或输入状态。
    void closeEvent(QCloseEvent*) override;
  private:
    // 切换全屏。
    void toggleFullScreen();
    // 刷新游戏联机。
    void refresh();
    // 刷新好友。
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
