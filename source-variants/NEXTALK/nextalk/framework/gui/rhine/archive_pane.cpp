#include "archive_pane.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlError>
#include <QTimer>
#include <QVBoxLayout>

static void initializeRhineResources() { static const bool initialized = [] { Q_INIT_RESOURCE(rhine); return true; }(); Q_UNUSED(initialized); }
namespace ccfw::gui {
RhineArchivePane::RhineArchivePane(QWidget* parent) : QWidget(parent) {
    initializeRhineResources();
    setObjectName("rhineArchivePane"); setMinimumWidth(180);
    auto* box = new QVBoxLayout(this); box->setContentsMargins(0, 0, 18, 0); box->setSpacing(10);
    auto* eyebrow = new QLabel("RHINE / COMMUNICATION OS"); eyebrow->setObjectName("rhineEyebrow"); box->addWidget(eyebrow);
    auto* title = new QLabel(QStringLiteral("让信息，只抵达彼此。")); title->setWordWrap(true); title->setObjectName("rhineHeroTitle"); box->addWidget(title);
    auto* description = new QLabel(QStringLiteral("连接联系人，开启你的私密通讯。")); description->setWordWrap(true); description->setObjectName("rhineDescription"); box->addWidget(description);
    view_ = new QQuickWidget; view_->setObjectName("rhineArchiveView");
    view_->setResizeMode(QQuickWidget::SizeRootObjectToView); view_->setMinimumHeight(200);
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view_->setAccessibleName(QStringLiteral("莱茵三维档案阵列"));
    view_->setAccessibleDescription(QStringLiteral("滚轮切换档案；点击查看模型后可拖动旋转。"));
    box->addWidget(view_, 1);
    caption_ = new QLabel("ARCHIVE / 01     —     PRIVATE CHANNEL"); caption_->setObjectName("rhineArchiveCaption"); caption_->setWordWrap(true); box->addWidget(caption_);
    auto* actions = new QHBoxLayout; actions->setSpacing(4);
    auto* previous = new QPushButton(QStringLiteral("←")); previous->setObjectName("rhinePrevious"); previous->setAccessibleName(QStringLiteral("上一个档案"));
    auto* next = new QPushButton(QStringLiteral("→")); next->setObjectName("rhineNext"); next->setAccessibleName(QStringLiteral("下一个档案"));
    previous->setFixedWidth(34); next->setFixedWidth(34);
    inspect_ = new QPushButton(QStringLiteral("查看模型 ↗")); inspect_->setObjectName("rhineInspect"); inspect_->setCheckable(true);
    motion_ = new QPushButton(QStringLiteral("暂停动效")); motion_->setObjectName("rhineMotion"); motion_->setCheckable(true);
    for (auto* control : {previous, next, inspect_, motion_}) { control->setCursor(Qt::PointingHandCursor); actions->addWidget(control); }
    actions->addStretch(); box->addLayout(actions);
    connect(previous, &QPushButton::clicked, this, [this] { if (auto* root = view_->rootObject()) root->setProperty("selectedRow", (root->property("selectedRow").toInt() + 14) % 15); });
    connect(next, &QPushButton::clicked, this, [this] { if (auto* root = view_->rootObject()) root->setProperty("selectedRow", (root->property("selectedRow").toInt() + 1) % 15); });
    connect(inspect_, &QPushButton::toggled, this, [this, previous, next](bool inspect) {
        inspect_->setText(inspect ? QStringLiteral("返回阵列 ↙") : QStringLiteral("查看模型 ↗"));
        previous->setEnabled(!inspect); next->setEnabled(!inspect); syncScene();
    });
    connect(motion_, &QPushButton::toggled, this, [this](bool paused) { motion_->setText(paused ? QStringLiteral("开启动效") : QStringLiteral("暂停动效")); syncScene(); });
    connect(view_, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status == QQuickWidget::Error) {
            QStringList messages; for (const auto& error : view_->errors()) messages << error.toString();
            setProperty("sceneError", messages.join('\n'));
            caption_->setText(QStringLiteral("三维预览不可用，通讯功能可正常使用。"));
            inspect_->setEnabled(false); motion_->setEnabled(false);
        } else if (status == QQuickWidget::Ready) syncScene();
    });
}
void RhineArchivePane::loadScene() {
    if (!view_->source().isEmpty()) return;
    view_->setClearColor(dark_ ? QColor("#222420") : QColor("#E8E5E1"));
    view_->setSource(QUrl("qrc:/rhine/ArchiveView.qml"));
}
void RhineArchivePane::syncScene() {
    if (auto* root = view_->rootObject()) {
        root->setProperty("darkMode", dark_); root->setProperty("accent", accent_);
        root->setProperty("sceneActive", isVisible()); root->setProperty("motionEnabled", !motion_->isChecked());
        root->setProperty("inspecting", inspect_->isChecked());
    }
}
void RhineArchivePane::setAppearance(bool dark, const QColor& accent) {
    dark_ = dark; accent_ = accent;
    view_->setClearColor(dark ? QColor("#222420") : QColor("#E8E5E1")); syncScene();
}
void RhineArchivePane::setMotionEnabled(bool enabled) {
    motion_->setChecked(!enabled);
    motion_->setText(enabled ? QStringLiteral("暂停动效") : QStringLiteral("开启动效")); syncScene();
}
bool RhineArchivePane::motionEnabled() const { return !motion_->isChecked(); }
void RhineArchivePane::showEvent(QShowEvent* event) { QWidget::showEvent(event); loadScene(); syncScene(); }
void RhineArchivePane::hideEvent(QHideEvent* event) { QWidget::hideEvent(event); syncScene(); }
}
