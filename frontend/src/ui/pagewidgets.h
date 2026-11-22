#pragma once
#include <QFrame>
class QVBoxLayout;
class QScrollArea;
namespace Messager {
// 创建页面顶部的横幅组件，显示标题和说明。
QFrame* banner(const QString& text);
// 返回桌面控件使用的卡片。
QFrame* card();
// 返回桌面控件使用的page基础。
QWidget* pageBase(const QString& title, const QString& subtitle, QVBoxLayout** layout);
// 返回桌面控件使用的滚动Page。
QScrollArea* scrollPage(QWidget* body);
} // namespace Messager
