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
    explicit ArchiveField(QQuick3DObject* parent = nullptr);
    Q_INVOKABLE void advance(double seconds);
    Q_INVOKABLE void setMode(const QString& mode);
    // Opening film time in seconds; negative leaves cinematic control.
    Q_INVOKABLE void setCinematic(double shot);
    Q_INVOKABLE void stepRow(int direction);
    Q_INVOKABLE void stepColumn(int direction);
    Q_INVOKABLE void selectIndex(int index);
    Q_INVOKABLE void selectInstance(int instance);
    Q_INVOKABLE void selectOutgoing(int slot);
    Q_INVOKABLE void hoverInstance(int instance);
    Q_INVOKABLE void hoverOutgoing(int slot);
    Q_INVOKABLE int instanceRecord(int instance) const;
    Q_INVOKABLE int outgoingRecord(int slot) const;
    // Plane drag: lane/row are the projected screen vectors of one cell step.
    Q_INVOKABLE void dragStart(double x, double y, QPointF lane, QPointF row, double timeMs);
    Q_INVOKABLE void dragMove(double x, double y, double timeMs);
    // Returns true when the gesture moved the plane (so it is not a click).
    Q_INVOKABLE bool dragEnd(double x, double y, double timeMs);
    Q_INVOKABLE void cancelPointer();
    Q_INVOKABLE void setPointer(double x, double y);
    Q_INVOKABLE void dragRotate(double pixels);
    Q_INVOKABLE void spin();
    Q_INVOKABLE int outgoingCount() const { return static_cast<int>(outgoing_.size()); }
    Q_INVOKABLE QVariantMap outgoingAt(int slot) const;
    QVariantMap motion() const { return motion_; }
    void setMotion(const QVariantMap& motion);
    QVector3D cameraPosition() const { return cameraPosition_; }
    QVector3D cameraAim() const { return cameraAim_; }
    double fieldOfView() const { return fov_; }
    double fogNear() const { return fogNear_; }
    double fogFar() const { return fogFar_; }
    double focusDistance() const { return focusDistance_; }
    QVector3D modelPosition() const { return modelPosition_; }
    QVector3D modelRotation() const { return modelRotation_; }
    double modelQuality() const;
    double detailVisibility() const;
    double detailAmount() const { return detail_; }
    double revealAmount() const { return reveal_; }
    bool documentReady() const { return documentReady_; }
    bool canInspect() const { return canInspect_; }
    bool canBrowse() const;
    bool selectionSettled() const;
    bool dragging() const { return holding_ && dragActive_; }
    bool dark() const { return themeTarget_ > 0.5; }
    void setDark(bool dark);
    Q_INVOKABLE void setDarkImmediate(bool dark);
    double themeAmount() const;
    double selectedTheme() const { return selectedTheme_; }
    Q_INVOKABLE double outgoingTheme(int slot) const;
    int selectedIndex() const;
    int selectedLane() const;
signals:
    void frame();
    void motionChanged();
    void selectionChanged(int index, const QString& axis, int direction);
    void hoverChanged(int index);
protected:
    QByteArray getInstanceBuffer(int* instanceCount) override;
private:
    struct Cell { int lane = 2, row = 12; bool operator<(const Cell& o) const { return lane != o.lane ? lane < o.lane : row < o.row; } bool operator==(const Cell&) const = default; };
    struct Spring { double value = 0, velocity = 0; };
    struct Pulse { int row, lane; double time; };
    struct Outgoing { Cell cell; Spring lift; std::optional<double> returnY; double rotationY = 0; int label = 0; QVector3D position; double rotationX = 0; double quality = 0; };
    struct Momentum { double value = 0, velocity = 0, target = 0; int phase = 0; }; // 0 coasting, 1 snapping, 2 idle
    struct Sample { double lane, row, time; };
    bool on(const char* key) const;
    void select(Cell cell, const QString& axis, int direction);
    void setHover(std::optional<Cell> cell);
    void navigatePlane(double lane, double row);
    void stopMomentum();
    double field(double row, double lane, double time, double shot, bool cinematic) const;
    QVector3D cellPosition(Cell cell) const;
    Cell fileLocation(int index) const;
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
    double themeSample(Cell cell);
    QVector3D cameraPosition_{-62.26f, 35.98f, 43.28f}, cameraAim_{-0.5f, 1.1f, 0.4f}, modelPosition_, modelRotation_;
    double fov_ = 6.15, fogNear_ = 22, fogFar_ = 47, focusDistance_ = 140;
    bool documentReady_ = false, canInspect_ = false;
    std::vector<Cell> drawn_;
    QByteArray buffer_;
};
}
