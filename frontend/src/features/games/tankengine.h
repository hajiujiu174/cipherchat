#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <memory>
namespace Messager {
// Qt adapter around the MIT TankTrouble game objects and collision mathematics.
class TankEngine {
public:
    // 构造坦克游戏引擎，初始化后续操作需要的状态或依赖。
    TankEngine();
    // 销毁坦克游戏引擎时释放它持有的资源。
    ~TankEngine();
    // 重置坦克游戏引擎。
    void reset(const QJsonArray& walls = {}, const QJsonArray& spawns = {});
    // 返回坦克游戏引擎使用的输入。
    void input(int player, int buttons);
    // 返回坦克游戏引擎使用的指针输入。
    bool pointerInput(int player, const QPointF& target, bool fire);
    static constexpr double MaxSpeed = 100.0; // arena units / second
    static constexpr double StepSeconds = 0.020;
    static constexpr double BodyLength = 28.0;
    static constexpr double BodyWidth = 20.0;
    static constexpr double AimOnlyRadius = 36.0;
    static constexpr double BarrelLength = 21.0;
    // 加载移动快照。
    bool loadMovementSnapshot(const QJsonObject&);
    // 预测移动。
    void predictMovement(int player);
    // 推进一步坦克游戏引擎。
    void step(); // fixed 20 ms; call only on the room host
    // 返回坦克游戏引擎使用的快照。
    QJsonObject snapshot() const;
    // 判断给定的快照数据是否有效。
    static bool validSnapshot(const QJsonObject&);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
