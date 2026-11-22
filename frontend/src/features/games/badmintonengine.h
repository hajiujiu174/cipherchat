#pragma once
#include <QJsonObject>
#include <QJsonArray>
namespace Messager {
// Native adapter of b-z/StickFigureBadminton (see third_party/stickfigurebadminton).
class BadmintonEngine {
public:
    // 构造羽毛球规则引擎，初始化后续操作需要的状态或依赖。
    BadmintonEngine() {reset();}
    // 重置羽毛球规则引擎。
    void reset();
    // 返回羽毛球规则引擎使用的输入。
    void input(int player,int buttons);
    // 加载快照。
    bool loadSnapshot(const QJsonObject&);
    // 排队操作列表。
    void queueActions(int player,int mask);
    // 推进一步羽毛球规则引擎。
    void step(); // Original fixed 15 ms simulation, advanced only by the host.
    // 返回羽毛球规则引擎使用的computer输入。
    void computerInput();
    // 返回羽毛球规则引擎使用的快照。
    QJsonObject snapshot() const;
    // 判断给定的快照数据是否有效。
    static bool validSnapshot(const QJsonObject&);
private:
    struct Player {double x=0,y=522,vy=0;int swing=0;};
    Player players[2];
    double bx=0,by=0,vx=0,vy=0;
    int buttons[2]{},actions[2]{},score[2]{},frame=0,round=1,server=0,wait=0,lastHit=-1,hits=0;
    bool serving=true,finished=false;
    // 移动Player。
    void movePlayer(int player);
    // 返回羽毛球规则引擎使用的发球。
    void serve();
    // 返回羽毛球规则引擎使用的坐标点。
    void point(int winner);
    // 返回羽毛球规则引擎使用的挥拍。
    void swing(int player);
};
}
