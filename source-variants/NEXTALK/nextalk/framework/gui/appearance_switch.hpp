#pragma once
#include <QPainter>
#include <QPushButton>

namespace ccfw::gui {
class AppearanceSwitch final : public QPushButton {
public:
    explicit AppearanceSwitch(QWidget* parent = nullptr) : QPushButton(parent) {
        setObjectName("appearanceMode"); setCheckable(true); setFixedSize(94, 36);
        setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("深色模式"));
        connect(this, &QPushButton::toggled, this, [this](bool dark) { describe(dark); });
        describe(false);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const bool dark = isChecked();
        const QRectF track(1, 1, width()-2, height()-2);
        p.setPen(Qt::NoPen);
        p.setBrush(dark ? QColor("#373A41") : QColor("#D5DAE2"));
        p.drawRoundedRect(track, 17, 17);
        p.setBrush(dark ? QColor("#E2E2E2") : QColor("#FFFFFF"));
        p.drawEllipse(QRectF(dark ? width()-33 : 5, 5, 26, 26));
        p.setPen(dark ? QColor("#F0F2F5") : QColor("#414956"));
        p.drawText(QRect(dark ? 7 : 35, 1, 52, height()-2), Qt::AlignCenter, text());
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush); p.setPen(QPen(dark ? QColor("#DCE4F2") : QColor("#515366"), 1, Qt::DashLine));
            p.drawRoundedRect(QRectF(1,1,width()-2,height()-2), 17,17);
        }
    }
private:
    void describe(bool dark) {
        setText(dark ? QStringLiteral("深色") : QStringLiteral("浅色"));
        setToolTip(dark ? QStringLiteral("点击切换为浅色") : QStringLiteral("点击切换为深色"));
        update();
    }
};
}
