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
// 返回桌面控件使用的应用程序图标。
QIcon applicationIcon();
// 返回桌面控件使用的图形符号。
QIcon glyph(const QString& name, const QColor& color, int size = 24);
// 返回桌面控件使用的标签。
QLabel* label(const QString& text, int pixels = 0, const char* role = nullptr, bool bold = false);
// 返回桌面控件使用的按钮。
QPushButton* button(const QString& text, const char* variant = "ghost", const QString& icon = {});
// 绘制头像。
void paintAvatar(QPainter& p, const QRectF& r, const QString& initials, const QColor& color);
class Avatar final : public QWidget {
  public:
    // 构造桌面控件，初始化后续操作需要的状态或依赖。
    Avatar(QString initials, QColor color, int size = 44, QWidget* parent = nullptr);
    // 设置桌面控件使用的图片。
    void setImage(const QByteArray& png);

  protected:
    // 在 Qt 的绘制回调中渲染桌面控件的当前外观。
    void paintEvent(QPaintEvent*) override;

  private:
    QPixmap m_image;
    QString m_initials;
    QColor m_color;
};
class IconButton final : public QPushButton {
  public:
    // 构造桌面控件，初始化后续操作需要的状态或依赖。
    IconButton(const QString& icon, const QString& tooltip, QWidget* parent = nullptr);
};
class ConversationDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    // 绘制桌面控件。
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
    // 返回桌面控件的控件建议尺寸。
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
};
class MessageEditor final : public QPlainTextEdit {
    Q_OBJECT
  public:
    // 构造聊天输入框，初始化后续操作需要的状态或依赖。
    explicit MessageEditor(QWidget* parent = nullptr);
  signals:
    // 通知订阅方聊天输入框已发生提交操作。
    void submitted();

  protected:
    // 响应聊天输入框的按键按下，同步界面或输入状态。
    void keyPressEvent(QKeyEvent*) override;
    // 响应聊天输入框的输入法组合文字，同步界面或输入状态。
    void inputMethodEvent(QInputMethodEvent*) override;

  private:
    bool m_composing = false;
};
class MessageBubble final : public QFrame {
    Q_OBJECT
  public:
    // 构造消息气泡控件，初始化后续操作需要的状态或依赖。
    MessageBubble(const Message& message, int availableWidth, QWidget* parent = nullptr);
  signals:
    // 通知调用方已请求open文件操作。
    void openFileRequested();
    // 通知调用方已请求openGomoku操作。
    void openGomokuRequested();

  protected:
    // 在 Qt 的绘制回调中渲染消息气泡控件的当前外观。
    void paintEvent(QPaintEvent*) override;

  private:
    bool m_outgoing;
    bool m_ownMessage;
};
class TransferCard final : public PaperFrame {
    Q_OBJECT
  public:
    // 构造文件传输卡片，初始化后续操作需要的状态或依赖。
    explicit TransferCard(const Transfer& transfer, QWidget* parent = nullptr);
    // 刷新文件传输卡片。
    void refresh(const Transfer& transfer);
  signals:
    // 返回文件传输卡片使用的操作。
    void action(const QString& id, const QString& action);

  private:
    QString m_id;
    QLabel *m_status, *m_meta;
    QProgressBar* m_progress;
    QPushButton *m_primary, *m_cancel, *m_locate, *m_reveal;
    QString m_action;
};
} // namespace Messager
