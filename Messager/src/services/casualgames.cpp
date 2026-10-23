#include "casualgames.h"
#include <algorithm>
#include <cmath>
namespace Messager {
void JumpGame::reset(quint32 seed){m_rng.seed(seed);m_current={{0,0},48,29,0};m_pawn={0,0};m_landing={0,0};m_score=0;m_lost=m_perfect=false;generate();}
void JumpGame::generate(){
    const int side=(m_rng()%2)?1:-1;const double gap=155+m_rng()%76;
    m_next={m_current.center+QPointF(side*gap*.86,-gap*.51),double(std::max(28,40-m_score/12)),24,int(m_rng()%5)};
    m_next.ry=m_next.rx*.60;
}
double JumpGame::distance()const{const auto delta=m_next.center-m_pawn;return std::hypot(delta.x(),delta.y());}
bool JumpGame::jump(int heldMs){
    if(m_lost||heldMs<0)return false;
    const auto delta=m_next.center-m_pawn;const double length=distance();
    if(length<=0)return false;
    m_landing=m_pawn+delta/length*(std::clamp(heldMs,0,MaxChargeMs)*ChargeSpeed);
    auto contains=[this](const Platform& p){const auto e=m_landing-p.center;return std::pow(e.x()/p.rx,2)+std::pow(e.y()/p.ry,2)<=1;};
    if(contains(m_next)){
        const auto e=m_landing-m_next.center;m_perfect=std::hypot(e.x(),e.y())<=6;
        m_score+=m_perfect?2:1;m_current=m_next;m_pawn=m_landing;generate();return true;
    }
    m_perfect=false;
    if(contains(m_current)){m_pawn=m_landing;return true;}
    m_pawn=m_landing;m_lost=true;return true;
}
void SheepGame::reset(int level,quint32 seed){
    *this=SheepGame();m_level=level==2?2:1;m_rng.seed(seed);
    const int cols=m_level==1?3:5,rows=m_level==1?3:6;
    const double left=(720-(cols-1)*68-64-32)/2.0,top=m_level==1?160:68;
    for(int z=0;z<3;++z)for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){
        const int id=m_tiles.size();m_tiles.append({id,0,z,QRectF(left+x*68+z*16,top+y*68+z*16,64,64),false});
    }
    // Assign triples in a valid removal order. Every generated board has at
    // least this solution without tools, even across layers and partial overlaps.
    while(m_solution.size()<m_tiles.size()){
        QVector<int> choices;for(const auto& t:m_tiles)if(exposed(t.id))choices.append(t.id);
        const int id=choices[int(m_rng()%choices.size())];m_tiles[id].removed=true;m_solution.append(id);
    }
    for(int at=0;at<m_solution.size();at+=3){const int kind=int(m_rng()%(m_level==1?5:8));for(int n=0;n<3;++n)m_tiles[m_solution[at+n]].kind=kind;}
    for(auto& t:m_tiles)t.removed=false;
}
bool SheepGame::exposed(int id)const{
    if(id<0||id>=m_tiles.size()||m_tiles[id].removed)return false;
    const auto& tile=m_tiles[id];
    for(const auto& other:m_tiles)if(!other.removed&&other.layer>tile.layer&&other.rect.intersects(tile.rect))return false;
    return true;
}
void SheepGame::saveUndo(){m_beforeTiles=m_tiles;m_beforeTray=m_tray;m_beforeReserve=m_reserve;m_beforeMoves=m_moves;m_hasUndo=true;}
void SheepGame::place(int id){
    const int kind=m_tiles[id].kind;int insert=m_tray.size();
    for(int n=0;n<m_tray.size();++n)if(m_tiles[m_tray[n]].kind==kind)insert=n+1;
    m_tray.insert(insert,id);int count=0;for(int item:m_tray)if(m_tiles[item].kind==kind)++count;
    if(count==3)for(int n=m_tray.size()-1;n>=0;--n)if(m_tiles[m_tray[n]].kind==kind)m_tray.removeAt(n);
    ++m_moves;evaluate();
}
int SheepGame::remaining()const{int n=m_tray.size()+m_reserve.size();for(const auto& t:m_tiles)if(!t.removed)++n;return n;}
void SheepGame::evaluate(){m_won=remaining()==0;m_lost=!m_won&&m_tray.size()>=7;}
bool SheepGame::pick(int id){if(m_won||m_lost||!exposed(id))return false;saveUndo();m_tiles[id].removed=true;place(id);return true;}
bool SheepGame::takeReserve(int position){
    if(m_won||m_lost||position<0||position>=m_reserve.size())return false;
    saveUndo();const int id=m_reserve.takeAt(position);place(id);return true;
}
bool SheepGame::undo(){
    if(!canUndo())return false;m_tiles=m_beforeTiles;m_tray=m_beforeTray;m_reserve=m_beforeReserve;m_moves=m_beforeMoves;
    m_undoUsed=true;m_hasUndo=false;evaluate();return true;
}
bool SheepGame::canShuffle()const{if(m_shuffleUsed||m_lost||m_won)return false;for(const auto& t:m_tiles)if(!t.removed)return true;return false;}
bool SheepGame::shuffle(){
    if(!canShuffle())return false;QVector<int> kinds;for(const auto& t:m_tiles)if(!t.removed)kinds.append(t.kind);
    std::shuffle(kinds.begin(),kinds.end(),m_rng);int at=0;for(auto& t:m_tiles)if(!t.removed)t.kind=kinds[at++];
    m_shuffleUsed=true;m_hasUndo=false;return true;
}
bool SheepGame::moveToReserve(){
    if(!canReserve())return false;for(int n=0;n<3;++n)m_reserve.append(m_tray.takeFirst());
    m_reserveUsed=true;m_hasUndo=false;evaluate();return true;
}
}
