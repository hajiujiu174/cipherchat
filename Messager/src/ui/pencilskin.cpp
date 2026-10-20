#include "pencilskin.h"
#include "theme.h"
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace Messager {
namespace {
QBrush grain(const QColor& base) {
    static QHash<QRgb, QBrush> cache;
    const auto found = cache.constFind(base.rgb());
    if (found != cache.cend())
        return found.value();
    QImage tile(128, 128, QImage::Format_RGB32);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            unsigned value = unsigned(x + y * 128 + 731);
            value ^= value >> 16;
            value *= 0x7feb352dU;
            value ^= value >> 15;
            const int delta = int(value % 5) - 2;
            tile.setPixelColor(x, y,
                               QColor(std::clamp(base.red() + delta, 0, 255),
                                      std::clamp(base.green() + delta, 0, 255),
                                      std::clamp(base.blue() + delta, 0, 255)));
        }
    const QBrush brush(tile);
    cache.insert(base.rgb(), brush);
    return brush;
}
void star(QPainter& p, QPointF center, double radius, QColor ink) {
    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
        const double a = -1.57079632679 + i * 3.14159265359 / 5, r = i % 2 ? radius * .43 : radius;
        const QPointF point(center.x() + std::cos(a) * r, center.y() + std::sin(a) * r);
        if (!i)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    p.setPen(QPen(ink, .9));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}
QPainterPath cloudBorder(const QRectF& r) {
    const double corner = std::min(11.0, r.height() / 4);
    QPainterPath path;
    path.moveTo(r.left() + corner, r.top());
    auto edge = [&](QPointF from, QPointF to, QPointF outward) {
        const int count = std::max(1, int(std::hypot(to.x() - from.x(), to.y() - from.y()) / 13));
        for (int i = 0; i < count; ++i) {
            const auto a = from + (to - from) * (double(i) / count),
                       b = from + (to - from) * (double(i + 1) / count);
            path.quadTo((a + b) / 2 + outward, b);
        }
    };
    edge({r.left() + corner, r.top()}, {r.right() - corner, r.top()}, {0, -2.7});
    path.quadTo(r.topRight(), {r.right(), r.top() + corner});
    edge({r.right(), r.top() + corner}, {r.right(), r.bottom() - corner}, {2.4, 0});
    path.quadTo(r.bottomRight(), {r.right() - corner, r.bottom()});
    edge({r.right() - corner, r.bottom()}, {r.left() + corner, r.bottom()}, {0, 2.7});
    path.quadTo(r.bottomLeft(), {r.left(), r.bottom() - corner});
    edge({r.left(), r.bottom() - corner}, {r.left(), r.top() + corner}, {-2.4, 0});
    path.quadTo(r.topLeft(), {r.left() + corner, r.top()});
    path.closeSubpath();
    return path;
}
} // namespace
void paintPencilPaper(QPainter& p, const QRectF& rect, const QColor& fill, bool decorations) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect, grain(fill));
    if (decorations && rect.width() > 100 && rect.height() > 120) {
        const bool dark = Theme::instance().dark();
        star(p, {rect.right() - 18, rect.top() + 18}, 6, QColor(dark ? "#b79d69" : "#dec179"));
        const QPointF at(rect.left() + 17, rect.bottom() - 20);
        QPainterPath cloud;
        cloud.moveTo(at + QPointF(0, 3));
        cloud.cubicTo(at + QPointF(-6, 3), at + QPointF(-6, -4), at + QPointF(0, -5));
        cloud.cubicTo(at + QPointF(-1, -13), at + QPointF(10, -14), at + QPointF(11, -6));
        cloud.cubicTo(at + QPointF(21, -8), at + QPointF(23, 4), at + QPointF(14, 4));
        cloud.closeSubpath();
        p.setPen(QPen(QColor(dark ? "#6d9b9e" : "#b8d7d4"), .9));
        p.setBrush(Qt::NoBrush);
        p.drawPath(cloud);
        const QPointF flower(rect.right() - 18, rect.bottom() - 20);
        p.setPen(QPen(QColor(dark ? "#a48baf" : "#cbb4d3"), .8));
        for (int i = 0; i < 5; ++i) {
            p.save();
            p.translate(flower);
            p.rotate(i * 72.0);
            p.drawEllipse(QRectF(-2, -9, 4, 6));
            p.restore();
        }
        p.drawEllipse(flower, 1.8, 1.8);
    }
    p.restore();
}
void paintPencilBubble(QPainter& p, const QRectF& rect, bool own, bool located) {
    const bool night = Theme::instance().dark();
    const QColor fill(own ? (night ? "#493344" : "#f8e6ef") : (night ? "#2d4049" : "#e5f2f9"));
    const QColor line = located
                            ? Theme::instance().colors().accent
                            : QColor(own ? (night ? "#c499b9" : "#bc99b8") : (night ? "#8bb7c9" : "#95b8cb"));
    const auto border = cloudBorder(rect.adjusted(4, 4, -4, -4));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(line, located ? 2 : 1.15));
    p.setBrush(grain(fill));
    p.drawPath(border);
    auto faint = line;
    faint.setAlpha(65);
    p.translate(.5, .4);
    p.setPen(QPen(faint, .5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(border);
    p.restore();
}
void paintPencilAvatar(QPainter& p, const QRectF& rect) {
    p.save();
    p.setClipping(false);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor gold(Theme::instance().dark() ? "#d2b477" : "#c4a469");
    p.setPen(QPen(gold, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect.adjusted(1.5, 1.5, -1.5, -1.5), rect.width() * .36, rect.height() * .36);
    star(p, {rect.right() - 4, rect.top() + 4}, 3, gold);
    p.restore();
}
PaperFrame::PaperFrame(QWidget* parent) : QFrame(parent) {
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}
void PaperFrame::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);
    if (!Theme::instance().pencil())
        return;
    const auto& c = Theme::instance().colors();
    const auto role = property("role").toString(), surface = property("surface").toString();
    const bool card = property("card").toBool(), composer = role == "composer";
    const QColor fill = surface == "rail"  ? c.rail
                        : role == "banner" ? c.accentSoft
                        : card || composer || surface == "white" || role == "header" || role == "detail"
                            ? c.surface
                            : c.window;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (card || composer || role == "banner") {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 14, 14);
        p.setClipPath(clip);
    }
    paintPencilPaper(p, rect(), fill, !card && !composer && role.isEmpty());
    p.setPen(QPen(c.border, 1));
    p.setBrush(Qt::NoBrush);
    if (card || composer)
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 13, 13);
    else if (surface == "rail")
        p.drawLine(width() - 1, 0, width() - 1, height());
    else if (role == "header")
        p.drawLine(0, height() - 1, width(), height() - 1);
    else if (role == "detail")
        p.drawLine(0, 0, 0, height());
}
void PencilButton::paintEvent(QPaintEvent* e) {
    QPushButton::paintEvent(e);
    if (!Theme::instance().pencil() || width() < 60 || height() < 24 ||
        property("variant").toString() == "ghost")
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto ink = Theme::instance().colors().accent;
    ink.setAlpha(isEnabled() ? 65 : 25);
    p.setPen(QPen(ink, .7));
    p.setBrush(Qt::NoBrush);
    const QRectF r = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
    QPainterPath line;
    line.moveTo(r.left() + 8, r.top());
    line.cubicTo(r.center().x(), r.top() - .2, r.center().x(), r.top() + .7, r.right() - 8, r.top());
    line.quadTo(r.right(), r.top(), r.right(), r.top() + 8);
    line.lineTo(r.right() - .4, r.bottom() - 8);
    line.quadTo(r.right(), r.bottom(), r.right() - 8, r.bottom());
    line.cubicTo(r.center().x(), r.bottom() + .3, r.center().x(), r.bottom() - .3, r.left() + 8, r.bottom());
    line.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - 8);
    line.lineTo(r.left() + .5, r.top() + 8);
    line.quadTo(r.left(), r.top(), r.left() + 8, r.top());
    p.drawPath(line);
}
} // namespace Messager
