#include "pagewidgets.h"
#include "components.h"
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
namespace Messager {
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
QScrollArea* scrollPage(QWidget* body) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);
    return scroll;
}
QFrame* card() {
    auto* f = new PaperFrame;
    f->setProperty("card", true);
    return f;
}

} // namespace Messager
