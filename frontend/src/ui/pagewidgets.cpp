#include "pagewidgets.h"
#include "components.h"
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
namespace Messager {
// 创建页面顶部的横幅组件，显示标题和说明。
QFrame* banner(const QString& text) {
    auto* f = new PaperFrame;
    f->setProperty("role", "banner");
    auto* h = new QHBoxLayout(f);
    h->setContentsMargins(14, 10, 14, 10);
    auto* l = label(text, 12, "accent");
    l->setWordWrap(true);
    h->addWidget(l);
    return f;
}
// 返回桌面控件使用的page基础。
QWidget* pageBase(const QString& title, const QString& subtitle, QVBoxLayout** layout) {
    auto* page = new PaperFrame;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(38, 30, 38, 28);
    v->setSpacing(22);
    auto* head = new QVBoxLayout;
    head->setSpacing(7);
    head->addWidget(label(title, 27, nullptr, true));
    auto* s = label(subtitle, 13, "muted");
    s->setWordWrap(true);
    head->addWidget(s);
    v->addLayout(head);
    *layout = v;
    return page;
}
// 返回桌面控件使用的滚动Page。
QScrollArea* scrollPage(QWidget* body) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);
    return scroll;
}
// 返回桌面控件使用的卡片。
QFrame* card() {
    auto* f = new PaperFrame;
    f->setProperty("card", true);
    return f;
}

} // namespace Messager
