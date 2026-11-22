#pragma once
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QWidget>

namespace ccfw::gui::pencil {
enum class Tone { Paper, Cream, Rose, Card };
// 判断客户端界面的是否已启用。
bool enabled(const QWidget* widget);
// 判断客户端界面的是否使用深色模式。
bool dark(const QWidget* widget);
// 返回客户端界面使用的强调色。
QColor accent(bool darkMode);
// 返回客户端界面使用的调色板。
QPalette palette(QPalette base, bool darkMode);
// 返回客户端界面使用的样式表。
QString stylesheet(bool darkMode);
// 返回客户端界面使用的气泡样式表。
QString bubbleStylesheet(bool darkMode);
class Paper final : public QWidget {
public:
    // 构造客户端界面，初始化后续操作需要的状态或依赖。
    explicit Paper(Tone tone=Tone::Paper,bool decorations=true,QWidget* parent=nullptr);
protected:
    // 在 Qt 的绘制回调中渲染客户端界面的当前外观。
    void paintEvent(QPaintEvent* event) override;
private:
    Tone tone_; bool decorations_;
};
class Bubble final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    // 在 Qt 的绘制回调中渲染客户端界面的当前外观。
    void paintEvent(QPaintEvent* event) override;
};
class Button final : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    // 在 Qt 的绘制回调中渲染客户端界面的当前外观。
    void paintEvent(QPaintEvent* event) override;
};
class Avatar final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    // 在 Qt 的绘制回调中渲染客户端界面的当前外观。
    void paintEvent(QPaintEvent* event) override;
};
}
