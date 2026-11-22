#pragma once
#include <QJsonObject>
#include <QJsonArray>
namespace Messager {
// Native adapter of b-z/StickFigureBadminton (see third_party/stickfigurebadminton).
class BadmintonEngine {
public:
    BadmintonEngine() {reset();}
    void reset();
    void input(int player,int buttons);
    bool loadSnapshot(const QJsonObject&);
    void queueActions(int player,int mask);
    void step(); // Original fixed 15 ms simulation, advanced only by the host.
    void computerInput();
    QJsonObject snapshot() const;
    static bool validSnapshot(const QJsonObject&);
private:
    struct Player {double x=0,y=522,vy=0;int swing=0;};
    Player players[2];
    double bx=0,by=0,vx=0,vy=0;
    int buttons[2]{},actions[2]{},score[2]{},frame=0,round=1,server=0,wait=0,lastHit=-1,hits=0;
    bool serving=true,finished=false;
    void movePlayer(int player);
    void serve();
    void point(int winner);
    void swing(int player);
};
}
