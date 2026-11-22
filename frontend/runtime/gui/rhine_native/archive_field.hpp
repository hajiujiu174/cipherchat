#pragma once
#include <QQuick3DInstancing>
#include <QPointF>
#include <QRectF>
#include <QVector3D>
#include <QVariantMap>
#include <map>
#include <optional>
#include <vector>

namespace ccfw::gui {
// Native port of RhineLabUI/src/scene.ts ArchiveScene.update(): the same
// springs, ripples, idle drift, opening cinematic, long-lens camera, 2D plane
// drag with momentum (archive-drag.ts) and pointer parallax. The instancing
// table positions every array cassette except the selected one and its copies.
class ArchiveField : public QQuick3DInstancing {
    Q_OBJECT
    Q_PROPERTY(double viewAspect MEMBER viewAspect_)
    // Normalized unobstructed area for the inspected model; the film ignores it.
    Q_PROPERTY(QRectF detailViewport MEMBER detailViewport_)
    Q_PROPERTY(QVariantMap motion READ motion WRITE setMotion NOTIFY motionChanged)
    Q_PROPERTY(QVector3D cameraPosition READ cameraPosition NOTIFY frame)
    Q_PROPERTY(QVector3D cameraAim READ cameraAim NOTIFY frame)
    Q_PROPERTY(double fieldOfView READ fieldOfView NOTIFY frame)
    Q_PROPERTY(double fogNear READ fogNear NOTIFY frame)
    Q_PROPERTY(double fogFar READ fogFar NOTIFY frame)
    Q_PROPERTY(double focusDistance READ focusDistance NOTIFY frame)
    Q_PROPERTY(QVector3D modelPosition READ modelPosition NOTIFY frame)
    Q_PROPERTY(QVector3D modelRotation READ modelRotation NOTIFY frame)
    Q_PROPERTY(double modelQuality READ modelQuality NOTIFY frame)
    Q_PROPERTY(double detailVisibility READ detailVisibility NOTIFY frame)
    Q_PROPERTY(double detailAmount READ detailAmount NOTIFY frame)
    Q_PROPERTY(double revealAmount READ revealAmount NOTIFY frame)
    Q_PROPERTY(bool documentReady READ documentReady NOTIFY frame)
    Q_PROPERTY(bool canInspect READ canInspect NOTIFY frame)
    Q_PROPERTY(bool canBrowse READ canBrowse NOTIFY frame)
    Q_PROPERTY(bool selectionSettled READ selectionSettled NOTIFY frame)
    Q_PROPERTY(bool dragging READ dragging NOTIFY frame)
    // Dark theme: ThemeWave from theme-motion.ts, one float per instance.
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY frame)
    Q_PROPERTY(double themeAmount READ themeAmount NOTIFY frame)
    Q_PROPERTY(double selectedTheme READ selectedTheme NOTIFY frame)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(int selectedLane READ selectedLane NOTIFY selectionChanged)
public:
    // 构造Rhine 档案浏览场景，初始化后续操作需要的状态或依赖。
    explicit ArchiveField(QQuick3DObject* parent = nullptr);
    // 获取Rhine 档案浏览场景对应的 advance，供调用方读取或参与计算。
    Q_INVOKABLE void advance(double seconds);
    // 设置Rhine 档案浏览场景使用的模式。
    Q_INVOKABLE void setMode(const QString& mode);
    // Opening film time in seconds; negative leaves cinematic control.
    // 设置Rhine 档案浏览场景使用的过场动画。
    Q_INVOKABLE void setCinematic(double shot);
    // 推进一步行。
    Q_INVOKABLE void stepRow(int direction);
    // 推进一步列。
    Q_INVOKABLE void stepColumn(int direction);
    // 选择索引。
    Q_INVOKABLE void selectIndex(int index);
    // 选择实例。
    Q_INVOKABLE void selectInstance(int instance);
    // 选择发出的。
    Q_INVOKABLE void selectOutgoing(int slot);
    // 返回Rhine 档案浏览场景使用的悬停实例。
    Q_INVOKABLE void hoverInstance(int instance);
    // 返回Rhine 档案浏览场景使用的悬停发出的。
    Q_INVOKABLE void hoverOutgoing(int slot);
    // 返回Rhine 档案浏览场景使用的实例记录。
    Q_INVOKABLE int instanceRecord(int instance) const;
    // 返回Rhine 档案浏览场景使用的发出的记录。
    Q_INVOKABLE int outgoingRecord(int slot) const;
    // Plane drag: lane/row are the projected screen vectors of one cell step.
    // 返回Rhine 档案浏览场景使用的拖动Start。
    Q_INVOKABLE void dragStart(double x, double y, QPointF lane, QPointF row, double timeMs);
    // 返回Rhine 档案浏览场景使用的拖动走法。
    Q_INVOKABLE void dragMove(double x, double y, double timeMs);
    // Returns true when the gesture moved the plane (so it is not a click).
    // 返回Rhine 档案浏览场景使用的拖动End。
    Q_INVOKABLE bool dragEnd(double x, double y, double timeMs);
    // 取消指针。
    Q_INVOKABLE void cancelPointer();
    // 设置Rhine 档案浏览场景使用的指针。
    Q_INVOKABLE void setPointer(double x, double y);
    // 返回Rhine 档案浏览场景使用的拖动Rotate。
    Q_INVOKABLE void dragRotate(double pixels);
    // 获取Rhine 档案浏览场景对应的 spin，供调用方读取或参与计算。
    Q_INVOKABLE void spin();
    // 返回Rhine 档案浏览场景使用的待展示对象数。
    Q_INVOKABLE int outgoingCount() const { return static_cast<int>(outgoing_.size()); }
    Q_INVOKABLE QVariantMap /* 返回Rhine 档案浏览场景使用的发出的At。 */ outgoingAt(int slot) const;
    // 返回Rhine 档案浏览场景使用的动效。
    QVariantMap motion() const { return motion_; }
    // 设置Rhine 档案浏览场景使用的动效。
    void setMotion(const QVariantMap& motion);
    // 返回Rhine 档案浏览场景使用的相机位置。
    QVector3D cameraPosition() const { return cameraPosition_; }
    // 返回Rhine 档案浏览场景使用的相机Aim。
    QVector3D cameraAim() const { return cameraAim_; }
    // 返回Rhine 档案浏览场景使用的字段Of视图。
    double fieldOfView() const { return fov_; }
    // 返回场景雾效开始作用的距离。
    double fogNear() const { return fogNear_; }
    // 返回场景雾效达到最大强度的距离。
    double fogFar() const { return fogFar_; }
    // 聚焦距离。
    double focusDistance() const { return focusDistance_; }
    // 返回Rhine 档案浏览场景使用的模型位置。
    QVector3D modelPosition() const { return modelPosition_; }
    // 返回Rhine 档案浏览场景使用的模型Rotation。
    QVector3D modelRotation() const { return modelRotation_; }
    // 返回Rhine 档案浏览场景使用的模型质量。
    double modelQuality() const;
    // 返回Rhine 档案浏览场景使用的详情可见性。
    double detailVisibility() const;
    // 返回Rhine 档案浏览场景使用的详情数值。
    double detailAmount() const { return detail_; }
    // 显示位置数值。
    double revealAmount() const { return reveal_; }
    // 返回Rhine 档案浏览场景使用的文档Ready。
    bool documentReady() const { return documentReady_; }
    // 判断Rhine 档案浏览场景当前是否允许Inspect。
    bool canInspect() const { return canInspect_; }
    // 判断Rhine 档案浏览场景当前是否允许Browse。
    bool canBrowse() const;
    // 返回Rhine 档案浏览场景使用的选中项已稳定。
    bool selectionSettled() const;
    // 报告鼠标拖动是否正在进行。
    bool dragging() const { return holding_ && dragActive_; }
    // 判断Rhine 档案浏览场景的是否使用深色模式。
    bool dark() const { return themeTarget_ > 0.5; }
    // 切换 Rhine 档案场景的深色主题。
    void setDark(bool dark);
    // 设置Rhine 档案浏览场景使用的立即切换深色模式。
    Q_INVOKABLE void setDarkImmediate(bool dark);
    // 返回Rhine 档案浏览场景使用的主题数值。
    double themeAmount() const;
    // 返回Rhine 档案浏览场景使用的选中的主题。
    double selectedTheme() const { return selectedTheme_; }
    // 返回Rhine 档案浏览场景使用的发出的主题。
    Q_INVOKABLE double outgoingTheme(int slot) const;
    // 返回Rhine 档案浏览场景使用的选中的索引。
    int selectedIndex() const;
    // 返回Rhine 档案浏览场景使用的选中的轨道。
    int selectedLane() const;
signals:
    // 返回Rhine 档案浏览场景使用的协议帧。
    void frame();
    // 通知监听方动效发生变化，供界面或调用方刷新。
    void motionChanged();
    // 通知监听方选中项发生变化，供界面或调用方刷新。
    void selectionChanged(int index, const QString& axis, int direction);
    // 通知监听方悬停发生变化，供界面或调用方刷新。
    void hoverChanged(int index);
protected:
    // 获取实例缓冲区。
    QByteArray getInstanceBuffer(int* instanceCount) override;
private:
    struct Cell { int lane = 2, row = 12; /* 比较两个Rhine 界面的数据，供相等判断或排序使用。 */ bool operator<(const Cell& o) const { return lane != o.lane ? lane < o.lane : row < o.row; } /* 比较两个Rhine 界面的数据，供相等判断或排序使用。 */ bool operator==(const Cell&) const = default; };
    struct Spring { double value = 0, velocity = 0; };
    struct Pulse { int row, lane; double time; };
    struct Outgoing { Cell cell; Spring lift; std::optional<double> returnY; double rotationY = 0; int label = 0; QVector3D position; double rotationX = 0; double quality = 0; };
    struct Momentum { double value = 0, velocity = 0, target = 0; int phase = 0; }; // 0 coasting, 1 snapping, 2 idle
    struct Sample { double lane, row, time; };
    // 获取Rhine 档案浏览场景对应的 on，供调用方读取或参与计算。
    bool on(const char* key) const;
    // 选择Rhine 档案浏览场景。
    void select(Cell cell, const QString& axis, int direction);
    // 设置Rhine 档案浏览场景使用的悬停。
    void setHover(std::optional<Cell> cell);
    // 跳转到平面。
    void navigatePlane(double lane, double row);
    // 停止惯性。
    void stopMomentum();
    // 返回Rhine 档案浏览场景使用的字段。
    double field(double row, double lane, double time, double shot, bool cinematic) const;
    // 返回Rhine 档案浏览场景使用的单元格位置。
    QVector3D cellPosition(Cell cell) const;
    // 返回Rhine 档案浏览场景使用的文件位置。
    Cell fileLocation(int index) const;
    // 返回Rhine 档案浏览场景使用的文件At单元格。
    int fileAtCell(Cell cell) const;
    double time_ = 0, shot_ = -1;
    double viewAspect_ = 16.0 / 9.0;
    QRectF detailViewport_;
    QVariantMap motion_;
    bool looping_ = false;
    Cell selected_{2, 12};
    Spring lift_, rail_, shoulder_{12, 0}, laneFocus_{2, 0}, columnCamera_;
    std::optional<double> returnY_;
    double detail_ = 0, targetDetail_ = 0, reveal_ = 0, targetReveal_ = 0;
    double rotation_ = 0, targetRotation_ = 0, spin_ = 0;
    double scanTime_ = 29.1, scanBlend_ = 0, idleGain_ = 0, pulseGain_ = 1, lastInteraction_ = 0;
    std::vector<Pulse> pulses_;
    std::map<Cell, double> hoverLifts_;
    std::optional<Cell> hover_;
    std::vector<Outgoing> outgoing_;
    int columnMemory_[5] = {6, 3, 0, 9, 12};
    // Drag state (ArchiveDrag / ArchivePlaneMomentum).
    bool holding_ = false, dragActive_ = false, dragMoved_ = false, navigating_ = false;
    double dragX_ = 0, dragY_ = 0, pointerX_ = 0, pointerY_ = 0, lastMotion_ = -1e9;
    QPointF motionDirection_;
    std::optional<std::array<double, 4>> inverse_;
    std::vector<Sample> samples_;
    double startLane_ = 0, startRow_ = 0, dragLane_ = 0, dragRow_ = 0;
    std::optional<std::pair<double, double>> dragTrack_;
    std::optional<std::pair<Momentum, Momentum>> momentum_;
    double parallaxX_ = 0, parallaxY_ = 0;
    double themeTarget_ = 0, themeStart_ = -10, themeBackgroundFrom_ = 0, selectedTheme_ = 0;
    Cell themeOrigin_{2, 12};
    std::map<Cell, double> themeFrom_, themeLatest_;
    // 返回Rhine 档案浏览场景使用的主题采样值。
    double themeSample(Cell cell);
    QVector3D cameraPosition_{-62.26f, 35.98f, 43.28f}, cameraAim_{-0.5f, 1.1f, 0.4f}, modelPosition_, modelRotation_;
    double fov_ = 6.15, fogNear_ = 22, fogFar_ = 47, focusDistance_ = 140;
    bool documentReady_ = false, canInspect_ = false;
    std::vector<Cell> drawn_;
    QByteArray buffer_;
};
}
