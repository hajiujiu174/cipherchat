#pragma once
#include "theme.hpp"
#include <QPainter>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace ccfw::gui::theme {
inline QColor accentInk(const QColor& color) {
    auto linear = [](double c) { return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
    return QColor(luminance > .179 ? "#000000" : "#FFFFFF");
}
inline QString recolor(QString style, const QColor& accent) {
    if (!accent.isValid()) return style;
    static const QRegularExpression colors("#[0-9A-Fa-f]{6}");
    auto matches = colors.globalMatch(style); QString output; qsizetype start = 0;
    while (matches.hasNext()) {
        const auto match = matches.next(); const QColor source(match.captured());
        const auto hue = source.hslHue(); QColor result = source;
        if ((hue >= 150 && hue <= 180) || (hue >= 245 && hue <= 275)) {
            const auto literal = match.captured().toUpper();
            if (literal == "#1F7A6D" || literal == "#6151C6") result = accent;
            else result = QColor::fromHslF(std::max(qreal(0), qreal(accent.hslHueF())), accent.hslSaturationF() * .75, source.lightnessF());
        }
        output += style.mid(start, match.capturedStart() - start) + result.name(); start = match.capturedEnd();
    }
    return output + style.mid(start);
}
inline QPixmap accentMark(int size, const QColor& accent, bool roundedSquare) {
    QPixmap result(size * 2, size * 2); result.setDevicePixelRatio(2); result.fill(Qt::transparent);
    QPainter painter(&result); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(accent);
    if (roundedSquare) painter.drawRoundedRect(QRectF(0, 0, size, size), size * .29, size * .29);
    else painter.drawEllipse(QRectF(0, 0, size, size));
    const int inner = size * .6; painter.drawPixmap((size - inner) / 2, (size - inner) / 2, glyph(Glyph::Lock, accentInk(accent), inner));
    return result;
}
}
