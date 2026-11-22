#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <memory>
namespace Messager {
// Qt adapter around the MIT TankTrouble game objects and collision mathematics.
class TankEngine {
public:
    TankEngine();
    ~TankEngine();
    void reset(const QJsonArray& walls = {}, const QJsonArray& spawns = {});
    void input(int player, int buttons);
    bool pointerInput(int player, const QPointF& target, bool fire);
    static constexpr double MaxSpeed = 100.0; // arena units / second
    static constexpr double StepSeconds = 0.020;
    static constexpr double BodyLength = 28.0;
    static constexpr double BodyWidth = 20.0;
    static constexpr double AimOnlyRadius = 36.0;
    static constexpr double BarrelLength = 21.0;
    bool loadMovementSnapshot(const QJsonObject&);
    void predictMovement(int player);
    void step(); // fixed 20 ms; call only on the room host
    QJsonObject snapshot() const;
    static bool validSnapshot(const QJsonObject&);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
