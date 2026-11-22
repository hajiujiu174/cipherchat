#pragma once
#include <QWidget>
#include <QColor>
class QQuickWidget;
class QPushButton;
class QLabel;
namespace ccfw::gui {
class RhineArchivePane final : public QWidget {
public:
    // 构造客户端界面，初始化后续操作需要的状态或依赖。
    explicit RhineArchivePane(QWidget* parent = nullptr);
    // 设置客户端界面使用的外观。
    void setAppearance(bool dark, const QColor& accent);
    // 设置客户端界面使用的动效已启用。
    void setMotionEnabled(bool enabled);
    // 返回客户端界面使用的动效已启用。
    bool motionEnabled() const;
protected:
    // 响应客户端界面的显示，同步界面或输入状态。
    void showEvent(QShowEvent*) override;
    // 响应客户端界面的隐藏，同步界面或输入状态。
    void hideEvent(QHideEvent*) override;
private:
    // 加载场景。
    void loadScene();
    // 同步场景。
    void syncScene();
    QQuickWidget* view_ = nullptr;
    QPushButton *inspect_ = nullptr, *motion_ = nullptr;
    QLabel* caption_ = nullptr;
    bool dark_ = false;
    QColor accent_ = QColor("#B66832");
};
}
