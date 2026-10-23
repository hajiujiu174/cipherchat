#include "badmintonengine.h"
#include <cmath>
#include <algorithm>
namespace Messager {
void BadmintonEngine::reset() {
    players[0]={89,522,0,0};players[1]={901,522,0,0};
    for(int i=0;i<2;++i)buttons[i]=actions[i]=score[i]=0;
    frame=0;round=1;server=0;wait=0;lastHit=-1;hits=0;serving=true;finished=false;serve();
}
void BadmintonEngine::serve() {
    bx=players[server].x+(server==0?39:-39);by=players[server].y+51;vx=vy=0;lastHit=-1;serving=true;
}
void BadmintonEngine::input(int player,int value) {
    if(player<0||player>1||value<0||value>15)return;
    actions[player]|=(value&~buttons[player])&12;buttons[player]=value;
}
void BadmintonEngine::point(int winner) {
    ++score[winner];server=winner;wait=80;finished=score[winner]>=7;serving=false;vx=vy=0;by=619;
    for(int i=0;i<2;++i)buttons[i]=actions[i]=0;
}
void BadmintonEngine::swing(int i) {
    auto& p=players[i];if(p.swing>0)return;p.swing=18;
    if(serving) {if(i!=server)return;serving=false;vx=i==0?29:-29;vy=-29;lastHit=i;++hits;return;}
    if(lastHit==i||(i==0?bx>=495:bx<=495))return;
    const double dx=bx-p.x,dy=by-p.y,d=std::hypot(dx,dy);
    // Upstream getSpeed: racket range 45..150 and the rear exclusion sector.
    if(d<45||d>150||((i==0?dx<0:dx>0)&&std::abs(dy)*90<std::abs(dx)*79))return;
    double a=24*std::abs(dy)/d,b=24*std::abs(dx)/d;
    if(bx>p.x)a=-a;if(by<p.y)b=-b;
    if((i==0&&a<0)||(i==1&&a>0)) {a=-a;b=-b;}
    a+=i==0?12:-12;b-=12;
    if(by>p.y) {a=i==0?29:-29;b=-29;}
    vx=a;vy=b;lastHit=i;++hits;
}
bool BadmintonEngine::loadSnapshot(const QJsonObject& s) {
    if(!validSnapshot(s))return false;
    for(int i=0;i<2;++i){auto a=s["players"].toArray()[i].toArray();players[i]={a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toInt()};buttons[i]=actions[i]=0;score[i]=s["score"].toArray()[i].toInt();}
    auto b=s["ball"].toArray();bx=b[0].toDouble();by=b[1].toDouble();vx=b[2].toDouble();vy=b[3].toDouble();frame=s["frame"].toInt();round=s["round"].toInt();server=s["server"].toInt();wait=s["wait"].toInt();lastHit=s["lastHit"].toInt();hits=s["hits"].toInt();serving=s["serving"].toBool();finished=s["finished"].toBool();return true;
}
void BadmintonEngine::queueActions(int player,int mask){if(player>=0&&player<2)actions[player]|=mask&12;}
void BadmintonEngine::movePlayer(int i) {
        auto& p=players[i];if(p.swing>0)--p.swing;
        p.x+=((buttons[i]&2?1:0)-(buttons[i]&1?1:0))*5.5;
        p.x=std::clamp(p.x,i==0?89.0:548.0,i==0?442.0:901.0);
        if(serving&&server==i)p.x=i==0?std::min(p.x,280.0):std::max(p.x,710.0);
        if((actions[i]&4)&&p.y>=522)p.vy=10.5;
        p.vy-=0.55;p.y-=p.vy;if(p.y>522){p.y=522;p.vy=0;}
}
void BadmintonEngine::step() {
    ++frame;if(finished)return;
    if(wait>0) {if(--wait==0) {++round;players[0]={89,522,0,0};players[1]={901,522,0,0};for(int i=0;i<2;++i)buttons[i]=actions[i]=0;serve();}return;}
    for(int i=0;i<2;++i) {
        movePlayer(i);
    }
    if(serving){bx=players[server].x+(server==0?39:-39);by=players[server].y+51;}
    for(int i=0;i<2;++i) {if(actions[i]&8)swing(i);actions[i]=0;}
    if(serving)return;
    // Original air drag and gravity; swept net crossing prevents tunnelling.
    const double oldX=bx,oldY=by;vx*=0.955;vy+=0.37-vy*0.045;bx+=vx;by+=vy;
    if(bx<53){bx=106-bx;vx=std::abs(vx);}if(bx>937){bx=1874-bx;vx=-std::abs(vx);}
    if((oldX<495)!=(bx<495)) {
        const double crossY=oldY+(by-oldY)*(495-oldX)/(bx-oldX);
        if(crossY>=522&&crossY<=619){bx=990-bx;vx*=-0.5;}
    }
    if(by>=619)point(bx<495?1:0);
}
void BadmintonEngine::computerInput() {
    if(finished||wait){input(1,0);return;}
    const auto& p=players[1];int value=0;
    if(serving){if(server==1&&frame%45==0)value|=8;}
    else if(bx>495) {
        const double target=std::clamp(bx+55,548.0,901.0);
        if(target-p.x>10)value|=2;else if(p.x-target>10)value|=1;
        if(lastHit!=1&&std::hypot(bx-p.x,by-p.y)>=45&&std::hypot(bx-p.x,by-p.y)<=145&&frame%5==0)value|=8;
        if(by<400&&std::abs(bx-p.x)<120&&p.y>=522)value|=4;
    }
    input(1,value);
}
QJsonObject BadmintonEngine::snapshot() const {
    QJsonArray a;for(const auto& p:players)a.append(QJsonArray{p.x,p.y,p.vy,p.swing});
    return {{"frame",frame},{"round",round},{"players",a},{"ball",QJsonArray{bx,by,vx,vy}},{"score",QJsonArray{score[0],score[1]}},{"server",server},{"serving",serving},{"wait",wait},{"finished",finished},{"lastHit",lastHit},{"hits",hits}};
}
bool BadmintonEngine::validSnapshot(const QJsonObject& s) {
    auto number=[](const QJsonValue& v,double low,double high){return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=low&&v.toDouble()<=high;};
    auto integer=[&](const QJsonValue& v,int low,int high){return number(v,low,high)&&std::floor(v.toDouble())==v.toDouble();};
    if(!integer(s["frame"],0,1000000000)||!integer(s["round"],1,14)||!integer(s["wait"],0,80)||!integer(s["server"],0,1)||!integer(s["lastHit"],-1,1)||!integer(s["hits"],0,1000000000)||!s["serving"].isBool()||!s["finished"].isBool())return false;
    const auto p=s["players"].toArray(),b=s["ball"].toArray(),c=s["score"].toArray();if(p.size()!=2||b.size()!=4||c.size()!=2)return false;
    for(int i=0;i<2;++i){auto a=p[i].toArray();if(a.size()!=4||!number(a[0],i==0?89:548,i==0?442:901)||!number(a[1],300,522)||!number(a[2],-12,11)||!integer(a[3],0,18)||!integer(c[i],0,7))return false;}
    if(!number(b[0],0,990)||!number(b[1],-1000,619)||!number(b[2],-50,50)||!number(b[3],-50,50))return false;
    const bool won=c[0].toInt()==7||c[1].toInt()==7;
    return s["finished"].toBool()==won&&!(c[0].toInt()==7&&c[1].toInt()==7)&&s["round"].toInt()<=c[0].toInt()+c[1].toInt()+1;
}
}
