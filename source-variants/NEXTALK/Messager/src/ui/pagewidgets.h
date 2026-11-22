#pragma once
#include <QFrame>
class QVBoxLayout;
class QScrollArea;
namespace Messager {
QFrame* banner(const QString& text);
QFrame* card();
QWidget* pageBase(const QString& title, const QString& subtitle, QVBoxLayout** layout);
QScrollArea* scrollPage(QWidget* body);
} // namespace Messager
