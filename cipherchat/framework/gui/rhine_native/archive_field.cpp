#include "archive_field.hpp"
#include <QColor>
#include <QVector4D>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>

// Formulas follow RhineLabUI/src/motion.ts and scene.ts (MIT, (c) 2026 LBEILC);
// the original sources and license are retained in third_party/rhinelab.
namespace ccfw::gui {
namespace {
constexpr double columnSpacing = 5.2, rowSpacing = 0.62, inspectionLift = 4.05;
constexpr int columns = 5, filesPerColumn = 3;
// Records are ordered account, chat, contacts, groups, files; the array keeps
// the upstream five-column layout with account in the centre column.
constexpr int laneOfBlock[columns] = {2, 1, 0, 3, 4};
constexpr int blockOfLane[columns] = {2, 1, 0, 3, 4};
constexpr int poolLanes[9] = {0, 1, 2, 3, 4, -2, -1, 5, 6};
double clamp01(double t) { return std::clamp(t, 0.0, 1.0); }
double smooth(double t) { t = clamp01(t); return t * t * t * (10 + t * (-15 + 6 * t)); }
double ease(double t) { return smooth(t); }
double bell(double x, double width) { return std::exp(-0.5 * (x / width) * (x / width)); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
int wrap(int value, int count) { return ((value % count) + count) % count; }
int nearestOccurrence(int value, int center, int period) {
    return value + static_cast<int>(std::floor((center - value + period / 2.0) / period)) * period;
}
void damp(double& value, double& velocity, double target, double rate, double dt) {
    const double delta = value - target, impulse = velocity + rate * delta, decay = std::exp(-rate * dt);
    value = target + (delta + impulse * dt) * decay;
    velocity = (velocity - rate * impulse * dt) * decay;
}
double archiveWave(double row, double lane, double time) {
    const double t = time - 22, phase = row + (lane - 2) * 0.65, enter = smooth(t / 0.32);
    const double first = 3 + t * 19, returning = 32 - (t - 2.3) * 24;
    auto packet = [](double d) { return 2.5 * bell(d, 3.8) - 0.58 * bell(d - 6, 3.5); };
    return enter * (packet(phase - first) * (1 - smooth((t - 2.15) / 0.65))
        + packet(phase - returning) * smooth((t - 2.17) / 0.32) * (1 - smooth((t - 3.5) / 0.85)));
}
double extraction(double time) { return 0.4 * smooth((time - 25.58) / 0.82) + 2.95 * smooth((time - 27.55) / 1.3); }
double settlingWave(double distance, double time) {
    const double age = time - 25.05 - std::abs(distance) * 0.065;
    const double envelope = std::max(-0.42, 2.15 - 0.17 * (std::sqrt(distance * distance + 1) - 1));
    const double rise = smooth(age / 0.62);
    const double ring = age > 0 ? std::sin(age * 5.1) * std::exp(-age * 1.3) : 0;
    return envelope * (rise + 0.18 * ring * smooth(age / 0.16));
}
double selectionWave(double distance, double age) {
    if (age < 0 || age > 3.2) return 0;
    return 0.8 * smooth(age / 0.2) * std::exp(-age * 1.15) * std::cos((distance - age * 8) * 0.58) * bell(distance - age * 8, 3.4);
}
double columnStrength(double lane, double focus, double progress = 1) {
    const double selected = 0.25 + 0.75 * bell(lane - focus, 0.55);
    return 1 + (selected - 1) * smooth(progress);
}
double idleWave(double row, double lane, double time) {
    return 0.075 * std::sin(time * M_PI * 2 / 8 + row * 0.3 - lane * 0.45)
        + 0.027 * std::sin(time * M_PI * 2 / 13 - row * 0.17 + lane * 0.3);
}
double cinematicField(double row, double lane, double time, double center, double focus) {
    const double handoff = smooth((time - 24.95) / 0.45), selection = smooth((time - 25.4) / 0.95);
    const double shoulderTime = time + 0.3 * handoff * (1 - selection);
    return archiveWave(row, lane, time) * (1 - handoff)
        + settlingWave(row - center, shoulderTime) * columnStrength(lane, focus, (time - 25.4) / 0.95);
}
double returnStep(double angle, double dt) {
    const double next = angle * std::exp(-dt * 7);
    return std::abs(next) <= 0.001 ? 0 : next;
}
QVector3D direction(double yaw, double elevation) {
    return QVector3D(float(-std::sin(yaw) * std::cos(elevation)), float(std::sin(elevation)), float(std::cos(yaw) * std::cos(elevation)));
}
QVector3D mix(QVector3D a, QVector3D b, double t) { return a + (b - a) * float(t); }
}

ArchiveField::ArchiveField(QQuick3DObject* parent) : QQuick3DInstancing(parent) {
    modelPosition_ = cellPosition(selected_);
}
bool ArchiveField::on(const char* key) const { return motion_.value(QLatin1String(key), true).toBool(); }
void ArchiveField::setMotion(const QVariantMap& motion) {
    motion_ = motion;
    if (!on("dragMomentum")) { momentum_.reset(); columnCamera_.velocity = rail_.velocity = 0; }
    if (!on("pointerParallax")) parallaxX_ = parallaxY_ = 0;
    emit motionChanged();
}
QVector3D ArchiveField::cellPosition(Cell cell) const {
    return QVector3D(float((cell.lane - 2) * columnSpacing), -4.6f, float((cell.row - 15.5) * rowSpacing));
}
ArchiveField::Cell ArchiveField::fileLocation(int index) const {
    return {laneOfBlock[index / filesPerColumn], 12 + index % filesPerColumn};
}
int ArchiveField::fileAtCell(Cell cell) const {
    return blockOfLane[wrap(cell.lane, columns)] * filesPerColumn + wrap(cell.row - 12, filesPerColumn);
}
int ArchiveField::selectedIndex() const { return fileAtCell(selected_); }
int ArchiveField::selectedLane() const { return wrap(selected_.lane, columns); }
double ArchiveField::detailVisibility() const { return ease((detail_ - 0.25) / 0.55); }
double ArchiveField::modelQuality() const { return ease(lift_.value / 0.4); }
bool ArchiveField::canBrowse() const { return looping_ && !targetDetail_ && detail_ < 0.2 && reveal_ >= 0.8 && shot_ < 0; }
bool ArchiveField::selectionSettled() const {
    return std::abs(shoulder_.value - selected_.row) < 0.035 && std::abs(laneFocus_.value - selected_.lane) < 0.035
        && std::abs(shoulder_.velocity) < 0.1 && std::abs(laneFocus_.velocity) < 0.1;
}

void ArchiveField::setMode(const QString& mode) {
    cancelPointer();
    setHover(std::nullopt);
    looping_ = mode != "hidden";
    if (!looping_) {
        selected_ = fileLocation(fileAtCell(selected_));
        outgoing_.clear();
    }
    lastInteraction_ = time_;
    targetReveal_ = mode == "hidden" ? 0 : 1;
    targetDetail_ = mode == "detail" ? 1 : 0;
    if (mode != "detail") {
        targetRotation_ = 0;
        if (rotation_ != 0) returnY_ = modelPosition_.y();
    } else returnY_.reset();
    documentReady_ = false;
}
void ArchiveField::setCinematic(double shot) { shot_ = shot; }

void ArchiveField::setHover(std::optional<Cell> cell) {
    if (cell == hover_) return;
    hover_ = cell;
    emit hoverChanged(cell ? fileAtCell(*cell) : -1);
}

void ArchiveField::select(Cell cell, const QString& axis, int direction) {
    if (!navigating_) cancelPointer();
    setHover(std::nullopt);
    lastInteraction_ = time_;
    const bool changed = !(cell == selected_);
    if (looping_ && changed && lift_.value > 0.0001) {
        // The previous file descends independently while the new one rises.
        if (outgoing_.size() >= 4) outgoing_.erase(outgoing_.begin());
        outgoing_.push_back({selected_, lift_, rotation_ != 0 ? std::optional<double>(modelPosition_.y()) : std::nullopt,
            rotation_, selectedIndex(), modelPosition_, modelRotation_.x(), modelQuality()});
        lift_ = {};
    }
    selected_ = cell;
    if (changed) { rotation_ = 0; returnY_.reset(); spin_ = 0; }
    const auto returning = std::find_if(outgoing_.begin(), outgoing_.end(), [&](const Outgoing& o) { return o.cell == cell; });
    if (returning != outgoing_.end()) {
        lift_ = returning->lift; rotation_ = returning->rotationY; returnY_ = returning->returnY;
        outgoing_.erase(returning);
    }
    if (on("selectionWave")) pulses_.push_back({cell.row, cell.lane, time_});
    targetRotation_ = 0;
    columnMemory_[selectedLane()] = selectedIndex();
    emit selectionChanged(selectedIndex(), axis, direction);
}
void ArchiveField::stepRow(int direction) {
    if (!looping_) return;
    stopMomentum();
    select({selected_.lane, selected_.row + direction}, "row", direction);
}
void ArchiveField::stepColumn(int direction) {
    if (!looping_) return;
    stopMomentum();
    columnMemory_[selectedLane()] = selectedIndex();
    const int lane = selected_.lane + direction;
    const Cell next = fileLocation(columnMemory_[wrap(lane, columns)]);
    select({lane, nearestOccurrence(next.row, selected_.row, filesPerColumn)}, "lane", direction);
}
void ArchiveField::selectIndex(int index) {
    index = wrap(index, columns * filesPerColumn);
    const Cell next = fileLocation(index);
    const Cell cell = looping_ ? Cell{nearestOccurrence(next.lane, selected_.lane, columns), nearestOccurrence(next.row, selected_.row, filesPerColumn)} : next;
    if (cell == selected_) return;
    select(cell, "auto", 0);
}
void ArchiveField::selectInstance(int instance) {
    if (instance < 0 || instance >= int(drawn_.size()) || !canBrowse()) return;
    select(drawn_[instance], "cell", 0);
}
void ArchiveField::selectOutgoing(int slot) {
    if (slot < 0 || slot >= int(outgoing_.size()) || !canBrowse()) return;
    select(outgoing_[slot].cell, "cell", 0);
}
void ArchiveField::hoverInstance(int instance) {
    if (instance < 0 || instance >= int(drawn_.size()) || !canBrowse() || momentum_) setHover(std::nullopt);
    else setHover(drawn_[instance]);
}
void ArchiveField::hoverOutgoing(int slot) {
    if (slot < 0 || slot >= int(outgoing_.size()) || !canBrowse() || momentum_) setHover(std::nullopt);
    else setHover(outgoing_[slot].cell);
}
int ArchiveField::instanceRecord(int instance) const {
    return instance < 0 || instance >= int(drawn_.size()) ? -1 : fileAtCell(drawn_[instance]);
}
int ArchiveField::outgoingRecord(int slot) const {
    return slot < 0 || slot >= int(outgoing_.size()) ? -1 : fileAtCell(outgoing_[slot].cell);
}
void ArchiveField::dragRotate(double pixels) {
    if (canInspect_) targetRotation_ = std::clamp(targetRotation_ + pixels * 0.004, -0.8, 0.8);
}
void ArchiveField::spin() { if (canInspect_) spin_ = 2 * M_PI; }
void ArchiveField::setPointer(double x, double y) {
    if (!on("pointerParallax")) { parallaxX_ = parallaxY_ = 0; return; }
    parallaxX_ = x; parallaxY_ = y;
}

// --- Plane drag (archive-drag.ts ArchiveDrag + scene.ts moveArchive) ---
namespace {
double trackCoordinateLane(double value) { return value / columnSpacing + 2; }
double trackCoordinateRow(double value) { return (-value - 2.17) / rowSpacing + 15.5; }
double trackPositionLane(double c) { return (c - 2) * columnSpacing; }
double trackPositionRow(double c) { return -2.17 - (c - 15.5) * rowSpacing; }
// ArchiveMomentum.step: free coasting, then a short spring to the nearest cell.
void momentumStep(double& value, double& velocity, double& target, int& phase, double dt, bool coasting) {
    constexpr double friction = 2.4;
    if (coasting) {
        const double decay = std::exp(-friction * dt);
        value += velocity * (1 - decay) / friction;
        velocity *= decay;
        if (std::abs(velocity) < 0.6) { target = std::round(value + velocity / friction); phase = 1; }
    } else if (phase == 1) {
        constexpr double rate = 10;
        const double delta = value - target, impulse = velocity + rate * delta, decay = std::exp(-rate * dt);
        value = target + (delta + impulse * dt) * decay;
        velocity = (velocity - rate * impulse * dt) * decay;
        if (std::abs(value - target) < 0.0001 && std::abs(velocity) < 0.005) { value = target; velocity = 0; phase = 2; }
    }
}
}
void ArchiveField::stopMomentum() {
    if (momentum_) { columnCamera_.velocity = 0; rail_.velocity = 0; }
    momentum_.reset();
}
void ArchiveField::cancelPointer() {
    stopMomentum();
    holding_ = dragActive_ = false;
    dragTrack_.reset();
}
void ArchiveField::dragStart(double x, double y, QPointF lane, QPointF row, double timeMs) {
    if (!canBrowse()) return;
    dragMoved_ = momentum_.has_value();
    stopMomentum();
    columnCamera_.velocity = rail_.velocity = 0;
    holding_ = true; dragActive_ = false;
    dragX_ = pointerX_ = x; dragY_ = pointerY_ = y;
    dragLane_ = dragRow_ = 0;
    samples_ = {{0, 0, timeMs}};
    lastMotion_ = -1e9; motionDirection_ = {};
    startLane_ = columnCamera_.value; startRow_ = rail_.value;
    // Invert the camera's two projected tracks so the plane follows any pointer path.
    const double determinant = lane.x() * row.y() - row.x() * lane.y();
    const double area = std::hypot(lane.x(), lane.y()) * std::hypot(row.x(), row.y());
    if (std::isfinite(area) && area > 0 && std::abs(determinant) > area * 0.001)
        inverse_ = std::array<double, 4>{row.y() / determinant, -row.x() / determinant, -lane.y() / determinant, lane.x() / determinant};
    else inverse_.reset();
    setHover(std::nullopt);
}
void ArchiveField::dragMove(double x, double y, double timeMs) {
    if (!holding_) return;
    if (!canBrowse()) { cancelPointer(); return; }
    const double dx = x - dragX_, dy = y - dragY_, distance = std::hypot(dx, dy);
    if (distance > 7) dragMoved_ = true;
    if (!inverse_ || (!dragActive_ && distance < 10)) return;
    if (!dragActive_) { startLane_ = columnCamera_.value; startRow_ = rail_.value; }
    dragActive_ = true;
    const auto& m = *inverse_;
    const double laneValue = dx * m[0] + dy * m[1], rowValue = dx * m[2] + dy * m[3];
    const QPointF delta(x - pointerX_, y - pointerY_);
    if (std::hypot(delta.x(), delta.y()) > 1e-9) {
        lastMotion_ = timeMs;
        // A reversal starts a fresh velocity estimate, without locking either axis.
        if (delta.x() * motionDirection_.x() + delta.y() * motionDirection_.y() < 0 && !samples_.empty()) samples_ = {samples_.back()};
        motionDirection_ = delta;
    }
    pointerX_ = x; pointerY_ = y;
    dragLane_ = laneValue; dragRow_ = rowValue;
    if (!samples_.empty() && samples_.back().time == timeMs) samples_.back() = {laneValue, rowValue, timeMs};
    else samples_.push_back({laneValue, rowValue, timeMs});
    std::erase_if(samples_, [&](const Sample& s) { return timeMs - s.time > 120; });
    if (samples_.size() > 32) samples_.erase(samples_.begin(), samples_.end() - 32);
    lastInteraction_ = time_;
    dragTrack_ = std::pair{startLane_ + dragLane_ * columnSpacing, startRow_ - dragRow_ * rowSpacing};
    columnCamera_ = {dragTrack_->first, 0};
    rail_ = {dragTrack_->second, 0};
    navigatePlane(trackCoordinateLane(columnCamera_.value), trackCoordinateRow(rail_.value));
}
bool ArchiveField::dragEnd(double x, double y, double timeMs) {
    if (!holding_) return false;
    dragMove(x, y, timeMs);
    const bool wasActive = dragActive_, moved = dragMoved_;
    if (wasActive && on("dragMomentum")) {
        double laneVelocity = 0, rowVelocity = 0;
        if (samples_.size() >= 2 && timeMs - lastMotion_ <= 80 && samples_.back().time - samples_.front().time >= 8) {
            const double scale = 1000 / (samples_.back().time - samples_.front().time);
            laneVelocity = (samples_.back().lane - samples_.front().lane) * scale;
            rowVelocity = (samples_.back().row - samples_.front().row) * scale;
        }
        auto make = [](double value, double velocity) {
            return Momentum{value, velocity, std::round(value), std::abs(velocity) >= 0.75 ? 0 : 1};
        };
        momentum_ = std::pair{make(trackCoordinateLane(columnCamera_.value), laneVelocity), make(trackCoordinateRow(rail_.value), rowVelocity)};
    }
    holding_ = dragActive_ = false;
    dragTrack_.reset();
    return wasActive || moved;
}
void ArchiveField::navigatePlane(double laneCoordinate, double rowCoordinate) {
    const Cell goal{int(std::lround(laneCoordinate)), int(std::lround(rowCoordinate))};
    if (goal == selected_) return;
    const Cell from = selected_;
    const int steps = std::min(64, std::max(std::abs(goal.lane - from.lane), std::abs(goal.row - from.row)));
    // Select physical cells along the travelled segment; a held plane is never pulled away.
    navigating_ = true;
    for (int i = 1; i <= steps; ++i) {
        const Cell cell{int(std::lround(from.lane + (goal.lane - from.lane) * double(i) / steps)),
            int(std::lround(from.row + (goal.row - from.row) * double(i) / steps))};
        if (!(cell == selected_)) select(cell, "cell", 0);
    }
    navigating_ = false;
}

namespace {
double themeEase(double t) { t = clamp01(t); return t * t * (3 - 2 * t); }
}
void ArchiveField::setDark(bool dark) {
    const double target = dark ? 1 : 0;
    if (target == themeTarget_) return;
    themeBackgroundFrom_ = themeAmount();
    themeFrom_ = themeLatest_;
    themeTarget_ = target; themeStart_ = time_;
    themeOrigin_ = selected_;
}
void ArchiveField::setDarkImmediate(bool dark) {
    themeTarget_ = themeBackgroundFrom_ = dark ? 1 : 0;
    themeStart_ = time_ - 10; themeFrom_.clear();
}
double ArchiveField::themeAmount() const {
    return themeBackgroundFrom_ + (themeTarget_ - themeBackgroundFrom_) * themeEase((time_ - themeStart_) / 0.85);
}
double ArchiveField::themeSample(Cell cell) {
    // A material wave with a frozen origin and an interruptible per-card start.
    const double delay = std::min(0.6, std::abs(cell.row - themeOrigin_.row) * 0.034 + std::abs(cell.lane - themeOrigin_.lane) * 0.11);
    const auto it = themeFrom_.find(cell);
    const double from = it == themeFrom_.end() ? themeBackgroundFrom_ : it->second;
    const double value = from + (themeTarget_ - from) * themeEase((time_ - themeStart_ - delay) / 0.58);
    themeLatest_[cell] = value;
    return value;
}
double ArchiveField::outgoingTheme(int slot) const {
    if (slot < 0 || slot >= int(outgoing_.size())) return themeAmount();
    const auto it = themeLatest_.find(outgoing_[slot].cell);
    return it == themeLatest_.end() ? themeAmount() : it->second;
}

QVariantMap ArchiveField::outgoingAt(int slot) const {
    if (slot < 0 || slot >= int(outgoing_.size())) return {{"visible", false}};
    const auto& o = outgoing_[slot];
    return {{"visible", true}, {"position", o.position}, {"rotation", QVector3D(float(qRadiansToDegrees(o.rotationX)), float(qRadiansToDegrees(o.rotationY)), 0)},
        {"label", o.label}, {"quality", o.quality}};
}

double ArchiveField::field(double row, double lane, double time, double shot, bool cinematic) const {
    if (cinematic) return cinematicField(row, lane, shot, shoulder_.value, laneFocus_.value);
    const double height = archiveWave(row, lane, scanTime_) * scanBlend_;
    const double breathing = idleWave(row, lane, time) * idleGain_;
    double ripple = 0;
    for (const auto& p : pulses_) ripple += selectionWave(std::hypot(row - p.row, (lane - p.lane) * 2.2), time - p.time);
    const double pulse = std::clamp(ripple, -0.6, 0.6) * pulseGain_;
    return height + settlingWave(row - shoulder_.value, 26.56) * columnStrength(lane, laneFocus_.value) + breathing + pulse;
}

void ArchiveField::advance(double seconds) {
    const double dt = std::min(std::max(seconds, 0.0), 0.05);
    time_ += std::max(seconds, 0.0);
    const double time = time_;
    const bool cinematic = shot_ >= 0;
    const double shot = cinematic ? shot_ : 29.1;
    const bool selectionMotion = on("selectionTransition"), detailMotion = on("detailTransition");
    const double blend = 1 - std::exp(-dt * (selectionMotion ? 2.8 : 35));
    const double detailBlend = 1 - std::exp(-dt * (detailMotion ? 2.8 : 35));
    reveal_ = cinematic ? ease((shot - 22) / 0.4) : lerp(reveal_, targetReveal_, blend);
    if (spin_ > 0) { const double step = std::min(spin_, dt * 2.4); spin_ -= step; rotation_ += step; targetRotation_ = rotation_; if (spin_ <= 0) { rotation_ = std::remainder(rotation_, 2 * M_PI); targetRotation_ = 0; } }
    else rotation_ = targetDetail_ ? lerp(rotation_, targetRotation_, detailBlend) : returnStep(rotation_, dt);
    if (!canBrowse() && (holding_ || momentum_)) cancelPointer();
    if (!canBrowse()) setHover(std::nullopt);
    if (momentum_ && !cinematic) {
        // Both components coast together, preserving the released direction.
        auto& [lane, row] = *momentum_;
        const bool coasting = lane.phase == 0 || row.phase == 0;
        momentumStep(lane.value, lane.velocity, lane.target, lane.phase, std::min(dt, 0.25), coasting);
        momentumStep(row.value, row.velocity, row.target, row.phase, std::min(dt, 0.25), coasting);
        navigatePlane(lane.value, row.value);
        lastInteraction_ = time;
        columnCamera_ = {trackPositionLane(lane.value), lane.velocity * columnSpacing};
        rail_ = {trackPositionRow(row.value), -row.velocity * rowSpacing};
        if (lane.phase == 2 && row.phase == 2) momentum_.reset();
    }
    if (cinematic) { scanTime_ = shot; scanBlend_ = 1; }
    else { scanTime_ += dt; scanBlend_ *= std::exp(-dt * 3); }

    const Cell hoverCell = hover_.value_or(Cell{INT_MIN, INT_MIN});
    if (hover_ && !hoverLifts_.contains(hoverCell)) hoverLifts_[hoverCell] = 0;
    for (auto it = hoverLifts_.begin(); it != hoverLifts_.end();) {
        const double target = it->first == hoverCell && !cinematic ? 0.28 : 0;
        it->second = lerp(it->second, target, 1 - std::exp(-dt * 14));
        if (target == 0 && it->second < 0.0001) it = hoverLifts_.erase(it); else ++it;
    }
    auto hoverLift = [&](Cell c) { const auto it = hoverLifts_.find(c); return it == hoverLifts_.end() ? 0.0 : it->second; };

    const QVector3D chosen = cellPosition(selected_);
    damp(shoulder_.value, shoulder_.velocity, selected_.row, selectionMotion ? 5 : 35, dt);
    damp(laneFocus_.value, laneFocus_.velocity, selected_.lane, selectionMotion ? 4 : 35, dt);
    // A held or freely coasting plane owns both tracks; selection cannot pull it.
    if (!holding_ && !momentum_) {
        damp(columnCamera_.value, columnCamera_.velocity, chosen.x(), selectionMotion ? 3.7 : 35, dt);
        damp(rail_.value, rail_.velocity, cinematic ? 0 : -2.17 - chosen.z(), selectionMotion ? 3.7 : 35, dt);
    }
    if (dragTrack_ && !cinematic) { columnCamera_ = {dragTrack_->first, 0}; rail_ = {dragTrack_->second, 0}; }
    if (cinematic) {
        rail_ = {}; lift_ = {extraction(shot), 0};
        shoulder_.value = selected_.row; laneFocus_ = {double(selected_.lane), 0}; columnCamera_ = {chosen.x(), 0};
    }
    const double trackX = cinematic ? 0 : columnCamera_.value;
    std::erase_if(pulses_, [&](const Pulse& p) { return time - p.time >= 3.2; });
    const bool aligningCopy = std::any_of(outgoing_.begin(), outgoing_.end(), [](const Outgoing& o) { return o.returnY.has_value(); });
    const bool idle = !cinematic && on("idleWave") && targetReveal_ > 0 && !targetDetail_ && detail_ < 0.01
        && !returnY_ && !aligningCopy && time - lastInteraction_ > 2.5;
    idleGain_ = cinematic ? 0 : lerp(idleGain_, idle ? 1 : 0, 1 - std::exp(-dt * (idle ? 0.8 : 4)));
    pulseGain_ = lerp(pulseGain_, targetDetail_ || returnY_ || aligningCopy ? 0 : 1, 1 - std::exp(-dt * 8));
    auto fieldAt = [&](double row, double lane) { return field(row, lane, time, shot, cinematic); };

    const double selectedBase = chosen.y() + fieldAt(selected_.row, selected_.lane);
    if (!cinematic) {
        if (returnY_ && rotation_ != 0) lift_ = {*returnY_ - selectedBase, 0};
        else {
            returnY_.reset();
            const bool blocked = std::any_of(outgoing_.begin(), outgoing_.end(), [&](const Outgoing& o) {
                return o.returnY && o.cell.lane == selected_.lane && std::abs(o.cell.row - selected_.row) < 5; });
            damp(lift_.value, lift_.velocity, targetDetail_ ? inspectionLift : blocked ? 0 : 0.4 * targetReveal_, detailMotion ? 4.2 : 35, dt);
        }
    }
    const double cameraTarget = targetDetail_ ? ease((lift_.value - 0.8) / 2.4)
        : returnY_ ? detail_ : ease((lift_.value - 0.4) / (inspectionLift - 0.4));
    if (cinematic) detail_ = 0.55 * ease((shot - 27.3) / 1.65) + 0.45 * ease((shot - 29.0) / 5.0);
    else detail_ = lerp(detail_, cameraTarget, detailBlend);
    const double detail = detail_;
    documentReady_ = detail > 0.78 && lift_.value > 3.3;

    const double entry = cinematic ? ease((shot - 21.9) / 0.86) : reveal_;
    const double entranceTime = clamp01((shot - 21.92) / 0.75);
    const double entryZ = cinematic ? -23 * (1 - entranceTime) * (1 - entranceTime) : -28 * (1 - entry);

    for (auto it = outgoing_.begin(); it != outgoing_.end();) {
        auto& o = *it;
        const QVector3D p = cellPosition(o.cell);
        const double baseY = p.y() + fieldAt(o.cell.row, o.cell.lane);
        o.rotationY = returnStep(o.rotationY, dt);
        if (o.returnY) { o.lift = {*o.returnY - baseY, 0}; if (o.rotationY == 0) o.returnY.reset(); }
        else damp(o.lift.value, o.lift.velocity, 0, detailMotion ? 4.5 : 35, dt);
        o.position = QVector3D(float(p.x() - trackX), float(baseY + o.lift.value + hoverLift(o.cell)), float(p.z() + entryZ + rail_.value));
        const double quality = ease(o.lift.value / 0.4);
        o.quality = quality;
        o.rotationX = (fieldAt(o.cell.row + 0.5, o.cell.lane) - fieldAt(o.cell.row - 0.5, o.cell.lane)) * 0.024 * (1 - detail) * (1 - quality);
        if (o.lift.value < 0.0001 && std::abs(o.rotationY) < 0.0001) it = outgoing_.erase(it); else ++it;
    }
    modelPosition_ = QVector3D(float(chosen.x() - trackX), float(selectedBase + lift_.value + hoverLift(selected_)), float(chosen.z() + entryZ + rail_.value));
    modelRotation_ = QVector3D(float(qRadiansToDegrees((fieldAt(selected_.row + 0.5, selected_.lane) - fieldAt(selected_.row - 0.5, selected_.lane))
        * 0.024 * (1 - detail) * (1 - ease(lift_.value / 0.4)))), float(qRadiansToDegrees(cinematic ? 0 : rotation_)), 0);

    // Camera: reference measurements from the upstream opening, then the long lens.
    const double orbit = ease((shot - 22.6) / 1.6), settle = ease((shot - 24.25) / 2.25);
    const double yaw = qDegreesToRadians(89 - 22 * orbit - 8 * settle);
    const double elevation = qDegreesToRadians(3 + 40 * ease((shot - 21.96) / 0.22) - 8 * orbit - 16 * settle);
    const double span = lerp(lerp(10.8, 10.3, orbit), 7.33, settle);
    const double distance = lerp(lerp(28 + 7 * orbit, 140, settle), 72, detail);
    QVector3D aim(-1.091f, float(lerp(-2.55 + 0.4 * orbit, -0.045, settle)), float(lerp(2.48, 0.481, settle)));
    QVector3D view = direction(yaw, elevation);
    if (cinematic) {
        const double earlyTurn = ease((shot - 27.3) / 1.3), finalTurn = ease((shot - 28.6) / 5.4);
        view = direction(yaw - qDegreesToRadians(9 * earlyTurn + 32 * finalTurn), elevation - qDegreesToRadians(1.5 * earlyTurn + 3.7 * finalTurn));
    } else view = mix(view, QVector3D(-0.277f, 0.238f, 0.931f), detail).normalized();
    const QVector3D right = QVector3D::crossProduct(QVector3D(0, 1, 0), view).normalized();
    const QVector3D up = QVector3D::crossProduct(view, right).normalized();
    if (cinematic) {
        const double pan = ease((shot - 25.4) / 0.95);
        aim += right * float(-2.05 * (1 - pan) * ease((shot - 24.2) / 0.8));
        if (shot >= 25.05 && shot <= 27.3) {
            const double carry = ease((shot - 25.4) / 1.05), pixelScale = 1080 / span;
            QVector3D anchor = modelPosition_ + QVector3D(-2.5f, 3.7f, 0);
            anchor += right * float(-(lerp(840, 518, carry) - 960) / pixelScale);
            anchor += up * float(-(540 - lerp(340, 288, carry)) / pixelScale);
            aim = mix(aim, anchor, ease((shot - 25.05) / 0.35));
        }
        if (shot > 27.3) {
            const double close = ease((shot - 27.3) / 6.7), extractionCamera = ease((shot - 27.3) / 1.25);
            const double screenX = lerp(518 - 98 * extractionCamera, 618, close), screenY = lerp(296 + 34 * extractionCamera, 287, close);
            const double pixelScale = 1080 / lerp(span, 5.9, detail);
            QVector3D anchor = modelPosition_ + QVector3D(-2.5f, 3.7f, 0);
            anchor += right * float(-(screenX - 960) / pixelScale);
            anchor += up * float(-(540 - screenY) / pixelScale);
            aim = mix(aim, anchor, ease((shot - 27.3) / 0.5));
        }
    }
    const double baseSpan = span + (5.9 - span) * detail;
    double viewSpan = cinematic ? baseSpan : std::max(baseSpan, baseSpan * (16.0 / 9.0) / viewAspect_);
    const bool framedDetail = !cinematic && detailViewport_.width() > 0 && detailViewport_.height() > 0;
    if (framedDetail) {
        // Fit the full cassette, including its rotation, in the area beside the
        // workspace. Keep the existing spring and the original cinematic frame.
        const double fitSpan = std::max({viewSpan, 6.2 / (detailViewport_.width() * viewAspect_), 5.0 / detailViewport_.height()});
        viewSpan = lerp(viewSpan, fitSpan, detail);
    }
    if (!cinematic) {
        QVector3D detailAim = modelPosition_ + QVector3D(0, 1.85f, 0);
        const QPointF center = framedDetail ? detailViewport_.center() : QPointF(550.0 / 1920, 560.0 / 1080);
        detailAim += right * float((0.5 - center.x()) * viewAspect_ * viewSpan);
        detailAim += up * float((center.y() - 0.5) * viewSpan);
        aim = mix(aim, detailAim, detail);
    }
    QVector3D target = aim + view * float(distance);
    if (!cinematic && on("pointerParallax")) { target.setX(target.x() + float(parallaxX_ * 0.12)); target.setY(target.y() - float(parallaxY_ * 0.12)); }
    const bool cameraMotion = targetDetail_ || detail_ > 0.01 ? detailMotion : selectionMotion;
    const double cameraBlend = cinematic || !cameraMotion ? 1 : 1 - std::exp(-dt * 5);
    cameraPosition_ = mix(cameraPosition_, target, cameraBlend);
    cameraAim_ = mix(cameraAim_, aim, cameraBlend);
    fov_ = lerp(fov_, qRadiansToDegrees(2 * std::atan(viewSpan / (2 * distance))), cameraBlend);
    const double rendered = (cameraPosition_ - cameraAim_).length();
    fogNear_ = rendered + lerp(5, -1, detail);
    fogFar_ = rendered + lerp(25, 12, detail);
    focusDistance_ = (modelPosition_ + QVector3D(0, 2, 0) - cameraPosition_).length();

    // Instance table: the visible window around the selected file, or the
    // upstream 160-cell pool during the opening film.
    drawn_.clear();
    if (cinematic || !looping_) {
        for (int i = 0; i < 160; ++i) drawn_.push_back({poolLanes[i / 32], i % 32});
    } else {
        const int laneCenter = int(std::lround(trackX / columnSpacing)) + 2;
        const int rowCenter = int(std::lround((-2.17 - rail_.value) / rowSpacing + 15.5));
        for (int lane = laneCenter - 4; lane <= laneCenter + 4; ++lane)
            for (int row = rowCenter - 22; row <= rowCenter + 22; ++row) drawn_.push_back({lane, row});
    }
    std::erase_if(drawn_, [&](const Cell& c) {
        return c == selected_ || std::any_of(outgoing_.begin(), outgoing_.end(), [&](const Outgoing& o) { return o.cell == c; }); });
    themeLatest_.clear();
    selectedTheme_ = themeSample(selected_);
    for (const auto& o : outgoing_) themeSample(o.cell);
    buffer_.resize(qsizetype(drawn_.size() * sizeof(InstanceTableEntry)));
    auto* entries = reinterpret_cast<InstanceTableEntry*>(buffer_.data());
    for (std::size_t i = 0; i < drawn_.size(); ++i) {
        const auto [lane, row] = drawn_[i];
        const double slope = fieldAt(row + 0.5, lane) - fieldAt(row - 0.5, lane);
        const QVector3D position(float((lane - 2) * columnSpacing - trackX), float(-4.6 + fieldAt(row, lane) + hoverLift(drawn_[i])),
            float((row - 15.5) * rowSpacing + entryZ + rail_.value));
        entries[i] = calculateTableEntry(position, QVector3D(1, 1, 1), QVector3D(float(qRadiansToDegrees(slope * 0.024 * (1 - detail))), 0, 0),
            Qt::white, QVector4D(float(themeSample(drawn_[i])), 0, 0, 0));
    }
    canInspect_ = !cinematic && targetDetail_ && detail > 0.9 && pulseGain_ < 0.01;
    markDirty();
    emit frame();
}

QByteArray ArchiveField::getInstanceBuffer(int* instanceCount) {
    if (instanceCount) *instanceCount = int(drawn_.size());
    return buffer_;
}
}
