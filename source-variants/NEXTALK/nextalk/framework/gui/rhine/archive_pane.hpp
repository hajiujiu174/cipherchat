#pragma once
#include <QWidget>
#include <QColor>
class QQuickWidget;
class QPushButton;
class QLabel;
namespace ccfw::gui {
class RhineArchivePane final : public QWidget {
public:
    explicit RhineArchivePane(QWidget* parent = nullptr);
    void setAppearance(bool dark, const QColor& accent);
    void setMotionEnabled(bool enabled);
    bool motionEnabled() const;
protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    void loadScene();
    void syncScene();
    QQuickWidget* view_ = nullptr;
    QPushButton *inspect_ = nullptr, *motion_ = nullptr;
    QLabel* caption_ = nullptr;
    bool dark_ = false;
    QColor accent_ = QColor("#B66832");
};
}
