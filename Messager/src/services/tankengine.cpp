#include "tankengine.h"
#include "../../third_party/tanktrouble/upstream/game/Maze.h"
#include "../../third_party/tanktrouble/upstream/game/Tank.h"
#include "../../third_party/tanktrouble/upstream/game/Shell.h"
#include "../../third_party/tanktrouble/upstream/game/Block.h"
#include "../../third_party/tanktrouble/upstream/game/util/Math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace Messager {
using namespace TankTrouble;
struct TankEngine::Impl {
    struct Bullet { std::unique_ptr<Shell> object; int age=0; };
    std::vector<Block> walls;
    std::array<std::unique_ptr<Tank>,2> tanks;
    std::vector<Bullet> bullets;
    std::array<int,2> buttons{}, cooldown{}, score{};
    std::array<bool,2> alive{true,true};
    std::array<bool,2> pointerActive{};
    std::array<QPointF,2> targets{};
    std::array<double,2> aim{};
    int frame=0, round=0, wait=0, nextId=2;
    bool finished=false;
    QJsonArray customWalls, customSpawns;
    bool blocked(const Object::PosInfo& pose) const {
        for(const auto& w:walls)if(util::checkRectRectCollision(pose.angle,pose.pos,TankEngine::BodyWidth,TankEngine::BodyLength,
            w.isHorizon()?0:90,w.center(),w.width(),w.height()))return true;
        return false;
    }
    void move(int i) {
        auto& tank=*tanks[i];const auto b=buttons[i];
        tank.forward(b&1);tank.backward(!(b&1)&&(b&2));tank.rotateCW(b&8);tank.rotateCCW(!(b&8)&&(b&4));
        auto next=tank.getNextPosition(2,3);
        if(pointerActive[i]) {
            const auto cur=tank.getCurrentPosition();next=cur;
            const auto target=targets[i];
            const double dx=target.x()-cur.pos.x(),dy=target.y()-cur.pos.y(),distance=std::hypot(dx,dy);
            if(distance>0.01) {
                const double desired=std::fmod(std::atan2(-dy,dx)*180.0/std::acos(-1.0)+360.0,360.0);
                aim[i]=desired;
                const double difference=std::remainder(desired-cur.angle,360.0);
                next.angle=std::fmod(cur.angle+std::clamp(difference,-3.0,3.0)+360.0,360.0);
                // Gun aim is independent; near the tank the pointer only turns it.
                if(distance>TankEngine::AimOnlyRadius) {
                    const double travel=std::min(TankEngine::MaxSpeed*TankEngine::StepSeconds,distance-TankEngine::AimOnlyRadius);
                    next.pos=util::Vec(cur.pos.x()+dx/distance*travel,cur.pos.y()+dy/distance*travel);
                }
            }
            tank.resetNextPosition(next);
        }
        // Separate rotation and translation: a rejected turn never traps movement
        // away from a wall. Sweep the solid body, including the middle, in <=1 px steps.
        const auto cur=tank.getCurrentPosition();auto pose=cur;
        const double rotation=std::remainder(next.angle-cur.angle,360.0);
        for(int n=1;n<=3;++n){auto turn=pose;turn.angle=std::fmod(cur.angle+rotation*n/3.0+360.0,360.0);if(blocked(turn))break;pose=turn;}
        const double dx=(next.pos.x()-cur.pos.x())/2,dy=(next.pos.y()-cur.pos.y())/2;
        for(int sub=0;sub<2;++sub){
            auto candidate=pose;candidate.pos=util::Vec(pose.pos.x()+dx,pose.pos.y()+dy);
            if(!blocked(candidate)){pose=candidate;continue;}
            candidate=pose;candidate.pos=util::Vec(pose.pos.x()+dx,pose.pos.y());if(!blocked(candidate))pose=candidate;
            candidate=pose;candidate.pos=util::Vec(pose.pos.x(),pose.pos.y()+dy);if(!blocked(candidate))pose=candidate;
        }
        tank.resetNextPosition(pose);tank.moveToNextPosition();
        if(!pointerActive[i])aim[i]=pose.angle;
        else {const double ax=targets[i].x()-pose.pos.x(),ay=targets[i].y()-pose.pos.y();if(std::hypot(ax,ay)>.01)aim[i]=std::fmod(std::atan2(-ay,ax)*180/std::acos(-1.0)+360,360.0);}
    }
    util::Vec muzzle(int i) const {
        const auto cur=tanks[i]->getCurrentPosition();auto position=cur.pos;
        // The gun is not a movement collider, but its muzzle may not create a
        // projectile on the far side of a wall that the barrel visually overlaps.
        for(double distance=.5;distance<=TankEngine::BarrelLength;distance+=.5){
            const auto next=util::polar2Cart(aim[i],distance,cur.pos);bool hit=false;
            for(const auto& w:walls){const auto c=w.center();
                const double hx=(w.isHorizon()?w.height():w.width())/2.0+Shell::RADIUS,
                    hy=(w.isHorizon()?w.width():w.height())/2.0+Shell::RADIUS;
                if(std::abs(next.x()-c.x())<=hx&&std::abs(next.y()-c.y())<=hy){hit=true;break;}}
            if(hit)break;position=next;
        }
        return position;
    }
    void arena() {
        walls.clear();bullets.clear();buttons={};cooldown={};pointerActive={};alive={true,true};wait=0;++round;
        if(customWalls.isEmpty()) {
            Maze maze;maze.generate();
            for(const auto& w:maze.getBlockPositions()) walls.emplace_back(int(walls.size()),w.first,w.second);
        } else for(auto value:customWalls) {
            auto a=value.toArray();walls.emplace_back(int(walls.size()),util::Vec(a[0].toDouble(),a[1].toDouble()),util::Vec(a[2].toDouble(),a[3].toDouble()));
        }
        for(auto a:std::array<std::array<double,4>,4>{{{0,0,660,0},{0,420,660,420},{0,0,0,420},{660,0,660,420}}})
            walls.emplace_back(int(walls.size()),util::Vec(a[0],a[1]),util::Vec(a[2],a[3]));
        for(int i=0;i<2;++i) {
            const auto spawn=customSpawns.size()==2 ? customSpawns[i].toArray() : QJsonArray{i==0?30:630,i==0?30:390,i==0?0:180};
            tanks[i]=std::make_unique<Tank>(i,util::Vec(spawn[0].toDouble(),spawn[1].toDouble()),spawn[2].toDouble());
            aim[i]=spawn[2].toDouble();
        }
    }
};
TankEngine::TankEngine():d(std::make_unique<Impl>()) { reset(); }
TankEngine::~TankEngine()=default;
void TankEngine::reset(const QJsonArray& walls,const QJsonArray& spawns) {
    for(auto value:walls) {
        auto a=value.toArray();
        if(a.size()!=4 || (a[0]!=a[2]&&a[1]!=a[3]))throw std::invalid_argument("invalid tank arena wall");
        for(int i=0;i<4;++i)if(!a[i].isDouble()||!std::isfinite(a[i].toDouble())||a[i].toDouble()<0||a[i].toDouble()>(i%2?420:660))throw std::invalid_argument("invalid tank arena coordinate");
    }
    if(!spawns.isEmpty()) {
        if(spawns.size()!=2)throw std::invalid_argument("invalid tank arena spawn count");
        for(auto value:spawns) {auto a=value.toArray();if(a.size()!=3)throw std::invalid_argument("invalid tank arena spawn");}
    }
    d=std::make_unique<Impl>();d->customWalls=walls;d->customSpawns=spawns;d->arena();
}
void TankEngine::input(int player,int buttons) {
    if(player>=0&&player<2) {d->buttons[player]=buttons&31;d->pointerActive[player]=false;}
}
bool TankEngine::pointerInput(int player,const QPointF& target,bool fire) {
    if(player<0||player>1||!std::isfinite(target.x())||!std::isfinite(target.y())||
       target.x()<0||target.x()>660||target.y()<0||target.y()>420)return false;
    d->targets[player]=target;d->pointerActive[player]=true;d->buttons[player]=fire?16:0;
    return true;
}
bool TankEngine::loadMovementSnapshot(const QJsonObject& s) {
    if(!validSnapshot(s))return false;auto next=std::make_unique<Impl>();
    for(auto value:s["walls"].toArray()){auto a=value.toArray();next->walls.emplace_back(int(next->walls.size()),util::Vec(a[0].toDouble(),a[1].toDouble()),util::Vec(a[2].toDouble(),a[3].toDouble()));}
    for(int i=0;i<2;++i){auto a=s["tanks"].toArray()[i].toArray();next->tanks[i]=std::make_unique<Tank>(i,util::Vec(a[0].toDouble(),a[1].toDouble()),a[2].toDouble());next->alive[i]=a[3].toBool();next->score[i]=s["score"].toArray()[i].toInt();}
    for(int i=0;i<2;++i)next->aim[i]=s.contains("aim")?s["aim"].toArray()[i].toDouble():s["tanks"].toArray()[i].toArray()[2].toDouble();
    next->frame=s["frame"].toInt();next->round=s["round"].toInt();next->wait=s["wait"].toInt();next->finished=s["finished"].toBool();d=std::move(next);return true;
}
void TankEngine::predictMovement(int i) {
    if(i>=0&&i<2&&d->alive[i]&&!d->finished&&!d->wait)d->move(i);
}
void TankEngine::step() {
    ++d->frame;
    if(d->finished)return;
    if(d->wait) { if(--d->wait==0)d->arena();return; }
    for(int i=0;i<2;++i) {
        if(!d->alive[i])continue;
        auto& tank=*d->tanks[i];const auto b=d->buttons[i];d->move(i);
        if(d->cooldown[i]>0)--d->cooldown[i];
        if((b&16)&&!d->cooldown[i]&&tank.remainShells()>0) {
            auto shell=std::unique_ptr<Shell>(tank.makeShell(d->nextId++));
            shell->resetNextPosition({d->muzzle(i),d->aim[i]});shell->moveToNextPosition();
            d->bullets.push_back({std::move(shell),0});d->cooldown[i]=15;
        }
    }
    std::array<bool,2> killed{};
    for(auto it=d->bullets.begin();it!=d->bullets.end();) {
        auto& shell=*it->object;bool expired=(++it->age>=400);
        // One-pixel substeps prevent thin-wall tunnelling at 200 px/s.
        for(int sub=0;sub<4&&!expired;++sub) {
            const auto cur=shell.getCurrentPosition();auto next=shell.getNextPosition(1,0);
            for(const auto& w:d->walls) {
                const auto c=w.center();
                const double hx=(w.isHorizon()?w.height():w.width())/2.0+Shell::RADIUS,hy=(w.isHorizon()?w.width():w.height())/2.0+Shell::RADIUS;
                if(std::abs(next.pos.x()-c.x())>=hx || std::abs(next.pos.y()-c.y())>=hy)continue;
                const bool hitX=std::abs(cur.pos.x()-c.x())>=hx;
                const bool hitY=std::abs(cur.pos.y()-c.y())>=hy;
                // Escape along the nearest face when a shell is born touching a wall.
                const bool flipX=hitX || (!hitY && hx-std::abs(next.pos.x()-c.x())<hy-std::abs(next.pos.y()-c.y()));
                next={cur.pos,flipX?util::angleFlipY(cur.angle):util::angleFlipX(cur.angle)};
                shell.countDown();expired=shell.ttl()<=0;break;
            }
            shell.resetNextPosition(next);shell.moveToNextPosition();
            for(int i=0;i<2&&!expired;++i) {
                if(!d->alive[i] || (shell.tankId()==i && shell.ttl()==Shell::INITIAL_TTL))continue;
                auto p=d->tanks[i]->getCurrentPosition();auto axes=util::getUnitVectors(p.angle);
                if(util::checkRectCircleCollision(axes.first,axes.second,p.pos,next.pos,BodyWidth,BodyLength,Shell::RADIUS)) {
                    killed[i]=true;expired=true;
                }
            }
        }
        if(expired) { d->tanks[shell.tankId()]->getRemainShell();it=d->bullets.erase(it); }
        else ++it;
    }
    for(int i=0;i<2;++i)if(killed[i])d->alive[i]=false;
    if(!d->alive[0] || !d->alive[1]) {
        if(d->alive[0]!=d->alive[1])++d->score[d->alive[0]?0:1];
        d->buttons={};d->finished=d->score[0]>=5||d->score[1]>=5;
        if(!d->finished)d->wait=100;
    }
}
QJsonObject TankEngine::snapshot() const {
    QJsonArray walls,tanks,shots;
    for(const auto& w:d->walls)walls.append(QJsonArray{w.start().x(),w.start().y(),w.end().x(),w.end().y()});
    for(int i=0;i<2;++i) {auto p=d->tanks[i]->getCurrentPosition();tanks.append(QJsonArray{p.pos.x(),p.pos.y(),p.angle,d->alive[i],d->tanks[i]->remainShells()});}
    for(const auto& b:d->bullets) {auto p=b.object->getCurrentPosition();shots.append(QJsonArray{p.pos.x(),p.pos.y(),p.angle,b.object->tankId(),b.object->ttl()});}
    return {{"frame",d->frame},{"round",d->round},{"wait",d->wait},{"finished",d->finished},
        {"score",QJsonArray{d->score[0],d->score[1]}},{"walls",walls},{"tanks",tanks},{"aim",QJsonArray{d->aim[0],d->aim[1]}},{"handling",2},{"shots",shots}};
}
bool TankEngine::validSnapshot(const QJsonObject& s) {
    auto integer=[](QJsonValue v,int low,int high) {return v.isDouble()&&v.toDouble()==v.toInt(-1)&&v.toInt()>=low&&v.toInt()<=high;};
    if(!integer(s["frame"],0,1000000000)||!integer(s["round"],1,1000000)||!integer(s["wait"],0,100)||!s["finished"].isBool())return false;
    const auto score=s["score"].toArray(),tanks=s["tanks"].toArray(),shots=s["shots"].toArray(),walls=s["walls"].toArray();
    if(score.size()!=2||tanks.size()!=2||shots.size()>10||walls.size()<4||walls.size()>140)return false;
    for(auto v:score)if(!integer(v,0,5))return false;
    auto finite=[](QJsonValue v,double max) {return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=0&&v.toDouble()<=max;};
    if(s.contains("aim")){auto aim=s["aim"].toArray();if(aim.size()!=2||!finite(aim[0],360)||!finite(aim[1],360))return false;}
    for(auto value:tanks) {auto a=value.toArray();if(a.size()!=5||!finite(a[0],660)||!finite(a[1],420)||!finite(a[2],360)||!a[3].isBool()||!integer(a[4],0,5))return false;}
    for(auto value:shots) {auto a=value.toArray();if(a.size()!=5||!finite(a[0],663)||!finite(a[1],423)||!finite(a[2],360)||!integer(a[3],0,1)||!integer(a[4],0,10))return false;}
    for(auto value:walls) {auto a=value.toArray();if(a.size()!=4||!finite(a[0],660)||!finite(a[1],420)||!finite(a[2],660)||!finite(a[3],420)||(a[0]!=a[2]&&a[1]!=a[3]))return false;}
    return true;
}
}

