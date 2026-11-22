#pragma once
#include <QColor>
#include <QFrame>
#include <QPushButton>
class QPainter;
namespace Messager {
// Rendering adapted from NEXTALK's original pencil_skin.cpp, without its legacy UI dependencies.
// 绘制铅笔皮肤Paper。
void paintPencilPaper(QPainter& painter, const QRectF& rect, const QColor& fill, bool decorations);
// 绘制铅笔皮肤气泡。
void paintPencilBubble(QPainter& painter, const QRectF& rect, bool outgoing, bool located);
// 用当前主题和头像信息绘制铅笔皮肤的头像外观。
void paintPencilAvatar(QPainter& painter, const QRectF& rect);
class PaperFrame : public QFrame {
  public:
    // 构造桌面控件，初始化后续操作需要的状态或依赖。
    explicit PaperFrame(QWidget* parent = nullptr);

  protected:
    // 在 Qt 的绘制回调中渲染桌面控件的当前外观。
    void paintEvent(QPaintEvent*) override;
};
class PencilButton : public QPushButton {
  public:
    using QPushButton::QPushButton;

  protected:
    // 在 Qt 的绘制回调中渲染桌面控件的当前外观。
    void paintEvent(QPaintEvent*) override;
};
} // namespace Messager
