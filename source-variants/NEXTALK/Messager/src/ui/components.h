#pragma once
#include "core/types.h"
#include "theme.h"
#include "pencilskin.h"
#include <QFrame>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
class QLabel;
class QProgressBar;
namespace Messager {
QIcon applicationIcon();
QIcon glyph(const QString& name, const QColor& color, int size = 24);
QLabel* label(const QString& text, int pixels = 0, const char* role = nullptr, bool bold = false);
QPushButton* button(const QString& text, const char* variant = "ghost", const QString& icon = {});
void paintAvatar(QPainter& p, const QRectF& r, const QString& initials, const QColor& color);
class Avatar final : public QWidget {
  public:
    Avatar(QString initials, QColor color, int size = 44, QWidget* parent = nullptr);
    void setImage(const QByteArray& png);

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    QPixmap m_image;
    QString m_initials;
    QColor m_color;
};
class IconButton final : public QPushButton {
  public:
    IconButton(const QString& icon, const QString& tooltip, QWidget* parent = nullptr);
};
class ConversationDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
};
class MessageEditor final : public QPlainTextEdit {
    Q_OBJECT
  public:
    explicit MessageEditor(QWidget* parent = nullptr);
  signals:
    void submitted();

  protected:
    void keyPressEvent(QKeyEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;

  private:
    bool m_composing = false;
};
class MessageBubble final : public QFrame {
    Q_OBJECT
  public:
    MessageBubble(const Message& message, int availableWidth, QWidget* parent = nullptr);
  signals:
    void openFileRequested();
    void openGomokuRequested();

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    bool m_outgoing;
    bool m_ownMessage;
};
class TransferCard final : public PaperFrame {
    Q_OBJECT
  public:
    explicit TransferCard(const Transfer& transfer, QWidget* parent = nullptr);
    void refresh(const Transfer& transfer);
  signals:
    void action(const QString& id, const QString& action);

  private:
    QString m_id;
    QLabel *m_status, *m_meta;
    QProgressBar* m_progress;
    QPushButton *m_primary, *m_cancel, *m_locate, *m_reveal;
    QString m_action;
};
} // namespace Messager
