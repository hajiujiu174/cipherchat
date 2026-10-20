#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QRegularExpressionValidator>
#include <functional>
#include <algorithm>

namespace ccfw::gui {
class ColorSpectrum final : public QWidget {
public:
    std::function<void(qreal, qreal)> changed;
    qreal hue = 0, saturation = 1, brightness = 1;
    explicit ColorSpectrum(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("colorSpectrum"); setMinimumSize(240, 200); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(QStringLiteral("色相与饱和度色谱"));
        setToolTip(QStringLiteral("横向选择色相，纵向选择饱和度；精确调整可使用右侧输入框。"));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient rainbow(0, 0, width(), 0);
        for (int i = 0; i <= 6; ++i) rainbow.setColorAt(i / 6.0, QColor::fromHsvF(i / 6.0, 1, 1));
        p.fillRect(rect(), rainbow);
        QLinearGradient white(0, 0, 0, height()); white.setColorAt(0, QColor(255,255,255,0)); white.setColorAt(1, Qt::white);
        p.fillRect(rect(), white);
        const QPointF point(hue * (width() - 1), (1 - saturation) * (height() - 1));
        p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::black, 4)); p.drawEllipse(point, 6, 6);
        p.setPen(QPen(Qt::white, 2)); p.drawEllipse(point, 6, 6);
    }
    void select(const QPointF& point) {
        hue = std::clamp(point.x() / std::max(1, width() - 1), 0.0, 0.99999);
        saturation = 1 - std::clamp(point.y() / std::max(1, height() - 1), 0.0, 1.0);
        update(); if (changed) changed(hue, saturation);
    }
    void mousePressEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) select(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override { if (event->buttons() & Qt::LeftButton) select(event->position()); }
};

// 无全局调色板：预览只作用于所属窗口，收藏在确认时提交到该数据目录。
class ThemeColorPicker final : public QDialog {
public:
    std::function<void(const QColor&)> preview;
    ThemeColorPicker(const QColor& initial, const QColor& defaultColor, const QStringList& saved, QWidget* parent)
        : QDialog(parent), color_(initial), favorites_(saved.mid(0, 24)) {
        setObjectName("themeColorDialog"); setWindowTitle(QStringLiteral("主题颜色")); setModal(true); resize(750, 580);
        setStyleSheet(QStringLiteral(
            "QDialog#themeColorDialog{background:#292929;}"
            "QDialog#themeColorDialog QWidget{color:#F4F4F4;font-family:'Microsoft YaHei UI';font-size:10pt;}"
            "QDialog#themeColorDialog QLabel{background:transparent;}"
            "QDialog#themeColorDialog QLineEdit,QDialog#themeColorDialog QSpinBox,QDialog#themeColorDialog QComboBox{background:#373737;border:1px solid #515151;border-radius:6px;padding:7px;color:#FFFFFF;selection-background-color:#666666;}"
            "QDialog#themeColorDialog QComboBox QAbstractItemView{background:#373737;color:white;selection-background-color:#555555;}"
            "QDialog#themeColorDialog QPushButton{background:#3B3B3B;color:white;border:1px solid #555555;border-radius:6px;padding:7px 13px;}"
            "QDialog#themeColorDialog QPushButton:hover{background:#505050;}"
            "QDialog#themeColorDialog QPushButton:focus{border:2px solid white;}"
            "QDialog#themeColorDialog QPushButton:disabled{color:#8C8C8C;}"
            "QDialog#themeColorDialog QSlider::groove:vertical{width:14px;border-radius:7px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 white,stop:1 black);}"
            "QDialog#themeColorDialog QSlider::handle:vertical{height:18px;margin:0 -4px;background:white;border:3px solid #666666;border-radius:11px;}"));
        auto* outer = new QVBoxLayout(this); outer->setContentsMargins(22,22,22,18); outer->setSpacing(18);
        auto* top = new QHBoxLayout; top->setSpacing(16);
        spectrum_ = new ColorSpectrum; top->addWidget(spectrum_, 1);
        sample_ = new QLabel; sample_->setObjectName("colorPreview"); sample_->setFixedWidth(44); top->addWidget(sample_);
        value_ = new QSlider(Qt::Vertical); value_->setObjectName("colorBrightness"); value_->setRange(0,255); value_->setFixedWidth(26);
        value_->setAccessibleName(QStringLiteral("亮度")); top->addWidget(value_);
        auto* inputs = new QVBoxLayout; inputs->setSpacing(12);
        hex_ = new QLineEdit; hex_->setObjectName("colorHex"); hex_->setAccessibleName(QStringLiteral("十六进制颜色"));
        hex_->setMaxLength(7); hex_->setMinimumWidth(135);
        hex_->setValidator(new QRegularExpressionValidator(QRegularExpression("#[0-9a-fA-F]{6}"), hex_)); inputs->addWidget(hex_);
        mode_ = new QComboBox; mode_->setObjectName("colorMode"); mode_->addItems({"RGB", "HSV"}); inputs->addWidget(mode_);
        auto* channels = new QGridLayout; channels->setVerticalSpacing(12);
        for (int i = 0; i < 3; ++i) {
            channels_[i] = new QSpinBox; channels_[i]->setObjectName(QStringLiteral("colorChannel%1").arg(i));
            channels_[i]->setRange(0,255); captions_[i] = new QLabel;
            channels->addWidget(channels_[i], i, 0); channels->addWidget(captions_[i], i, 1);
            connect(channels_[i], &QSpinBox::valueChanged, this, [this](int) { fromChannels(); });
        }
        inputs->addLayout(channels); inputs->addStretch(); top->addLayout(inputs); outer->addLayout(top, 1);
        auto* swatches = new QHBoxLayout; swatches->setSpacing(28);
        auto* basics = new QVBoxLayout; basics->addWidget(new QLabel(QStringLiteral("基本颜色")));
        auto* basicGrid = new QGridLayout; basicGrid->setSpacing(9);
        const QStringList colors = {"#F08080","#EF3427","#864747","#A95643","#6B291E","#9AF5FA","#4CF2E8","#3589F2",
            "#FFF995","#FFFF46","#F5A45E","#F0804D","#83501C","#899129","#8B80F9","#8236ED",
            "#A5FA82","#A0FA37","#55F230","#7CF290","#EE87C0","#E987F1","#E731D6","#EA3385",
            "#367D23","#327844","#367B7F","#1C4246","#752384","#462180","#000000","#FFFFFF"};
        for (int i = 0; i < colors.size(); ++i) basicGrid->addWidget(swatch(colors[i]), i / 8, i % 8);
        basics->addLayout(basicGrid); swatches->addLayout(basics);
        auto* custom = new QVBoxLayout; auto* customHead = new QHBoxLayout;
        customHead->addWidget(new QLabel(QStringLiteral("自定义颜色"))); customHead->addStretch();
        auto* add = new QPushButton("+"); add->setObjectName("saveCustomColor"); add->setFixedSize(30,30); add->setToolTip(QStringLiteral("收藏当前颜色"));
        add->setStyleSheet("QPushButton{padding:0;font-size:22px;}");
        customHead->addWidget(add); custom->addLayout(customHead);
        customGrid_ = new QGridLayout; customGrid_->setSpacing(9); custom->addLayout(customGrid_); swatches->addLayout(custom); outer->addLayout(swatches);
        auto* hint = new QLabel(QStringLiteral("实时预览 · 取消可还原 · 主题文字会自动调整对比度")); hint->setWordWrap(true); outer->addWidget(hint);
        auto* actions = new QHBoxLayout; auto* reset = new QPushButton(QStringLiteral("恢复默认")); reset->setObjectName("resetThemeColor");
        actions->addWidget(reset); actions->addStretch();
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons_->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用")); buttons_->button(QDialogButtonBox::Ok)->setObjectName("applyThemeColor");
        buttons_->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); buttons_->button(QDialogButtonBox::Cancel)->setObjectName("cancelThemeColor");
        actions->addWidget(buttons_); outer->addLayout(actions);
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(reset, &QPushButton::clicked, this, [this, defaultColor] { updateColor(defaultColor); });
        connect(add, &QPushButton::clicked, this, [this] {
            const auto name = color_.name().toUpper(); favorites_.removeAll(name); favorites_.prepend(name);
            if (favorites_.size() > 24) favorites_.removeLast();
            rebuildFavorites();
        });
        connect(hex_, &QLineEdit::textEdited, this, [this](const QString& text) {
            buttons_->button(QDialogButtonBox::Ok)->setEnabled(hex_->hasAcceptableInput());
            if (hex_->hasAcceptableInput()) updateColor(QColor(text));
        });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { sync(); });
        connect(value_, &QSlider::valueChanged, this, [this](int value) { updateColor(QColor::fromHsvF(spectrum_->hue, spectrum_->saturation, value / 255.0)); });
        spectrum_->changed = [this](qreal hue, qreal saturation) { updateColor(QColor::fromHsvF(hue, saturation, value_->value() / 255.0)); };
        rebuildFavorites(); sync();
    }
    QColor selectedColor() const { return color_; }
    QStringList favorites() const { return favorites_; }
private:
    QColor color_;
    QStringList favorites_;
    ColorSpectrum* spectrum_ = nullptr;
    QLabel* sample_ = nullptr;
    QSlider* value_ = nullptr;
    QLineEdit* hex_ = nullptr;
    QComboBox* mode_ = nullptr;
    QSpinBox* channels_[3]{};
    QLabel* captions_[3]{};
    QGridLayout* customGrid_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* swatch(const QString& value) {
        auto* button = new QPushButton; button->setFixedSize(25,25); button->setToolTip(value);
        button->setAccessibleName(value.isEmpty() ? QStringLiteral("空收藏位") : value);
        button->setStyleSheet(QStringLiteral("QPushButton{padding:0;border:1px %1 #898989;border-radius:12px;background:%2;} QPushButton:focus{border:2px solid white;}")
            .arg(value.isEmpty() ? "dashed" : "solid", value.isEmpty() ? "transparent" : value));
        button->setEnabled(!value.isEmpty()); connect(button, &QPushButton::clicked, this, [this, value] { updateColor(QColor(value)); }); return button;
    }
    void rebuildFavorites() {
        while (auto* item = customGrid_->takeAt(0)) { delete item->widget(); delete item; }
        for (int i = 0; i < 24; ++i) customGrid_->addWidget(swatch(i < favorites_.size() ? favorites_[i] : QString{}), i / 6, i % 6);
    }
    void updateColor(const QColor& color) { if (!color.isValid()) return; color_ = color; sync(); if (preview) preview(color_); }
    void fromChannels() {
        if (mode_->currentIndex() == 0) updateColor(QColor(channels_[0]->value(), channels_[1]->value(), channels_[2]->value()));
        else updateColor(QColor::fromHsvF(channels_[0]->value() / 360.0, channels_[1]->value() / 100.0, channels_[2]->value() / 100.0));
    }
    void sync() {
        const QSignalBlocker blockValue(value_), blockHex(hex_);
        hex_->setText(color_.name().toUpper()); value_->setValue(color_.value());
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(true);
        if (color_.hsvHueF() >= 0) spectrum_->hue = color_.hsvHueF();
        // Keep the cursor's hue/saturation when brightness reaches zero.
        if (color_.value() > 0) spectrum_->saturation = color_.hsvSaturationF();
        spectrum_->brightness = color_.valueF(); spectrum_->update();
        sample_->setStyleSheet("background:" + color_.name() + ";border:2px solid #454545;border-radius:5px;");
        const bool hsv = mode_->currentIndex() == 1;
        const int values[] = {hsv ? std::min(359, qRound(spectrum_->hue * 360)) : color_.red(), hsv ? qRound(color_.hsvSaturationF() * 100) : color_.green(), hsv ? qRound(color_.valueF() * 100) : color_.blue()};
        const QStringList names = hsv ? QStringList{QStringLiteral("色相"), QStringLiteral("饱和度 %"), QStringLiteral("明度 %")} : QStringList{QStringLiteral("红色"), QStringLiteral("绿色"), QStringLiteral("蓝色")};
        for (int i = 0; i < 3; ++i) { const QSignalBlocker block(channels_[i]); channels_[i]->setRange(0, hsv ? (i == 0 ? 359 : 100) : 255); channels_[i]->setValue(values[i]); captions_[i]->setText(names[i]); channels_[i]->setAccessibleName(names[i]); }
    }
};
}
