#pragma once
#include <QColor>
#include <QFrame>
#include <QPushButton>
class QPainter;
namespace Messager {
// Rendering adapted from CipherChat's original pencil_skin.cpp, without its legacy UI dependencies.
void paintPencilPaper(QPainter& painter, const QRectF& rect, const QColor& fill, bool decorations);
void paintPencilBubble(QPainter& painter, const QRectF& rect, bool outgoing, bool located);
void paintPencilAvatar(QPainter& painter, const QRectF& rect);
class PaperFrame : public QFrame {
  public:
    explicit PaperFrame(QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent*) override;
};
class PencilButton : public QPushButton {
  public:
    using QPushButton::QPushButton;

  protected:
    void paintEvent(QPaintEvent*) override;
};
} // namespace Messager
