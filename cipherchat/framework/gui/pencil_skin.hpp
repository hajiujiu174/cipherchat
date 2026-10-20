#pragma once
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QWidget>

namespace ccfw::gui::pencil {
enum class Tone { Paper, Cream, Rose, Card };
bool enabled(const QWidget* widget);
bool dark(const QWidget* widget);
QColor accent(bool darkMode);
QPalette palette(QPalette base, bool darkMode);
QString stylesheet(bool darkMode);
QString bubbleStylesheet(bool darkMode);
class Paper final : public QWidget {
public:
    explicit Paper(Tone tone=Tone::Paper,bool decorations=true,QWidget* parent=nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    Tone tone_; bool decorations_;
};
class Bubble final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent* event) override;
};
class Button final : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent* event) override;
};
class Avatar final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent* event) override;
};
}
