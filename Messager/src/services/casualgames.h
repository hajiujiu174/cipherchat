#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <random>
namespace Messager {
class JumpGame {
public:
    struct Platform {QPointF center;double rx=42,ry=25;int color=0;};
    void reset(quint32 seed=1);
    bool jump(int heldMs);
    const Platform& current()const{return m_current;}
    const Platform& next()const{return m_next;}
    QPointF pawn()const{return m_pawn;}
    QPointF lastLanding()const{return m_landing;}
    int score()const{return m_score;}
    bool lost()const{return m_lost;}
    bool perfect()const{return m_perfect;}
    double distance()const;
    static constexpr double ChargeSpeed=.28;
    static constexpr int MaxChargeMs=1200;
private:
    void generate();
    std::mt19937 m_rng{1};Platform m_current,m_next;QPointF m_pawn,m_landing;
    int m_score=0;bool m_lost=false,m_perfect=false;
};
class SheepGame {
public:
    struct Tile {int id=0,kind=0,layer=0;QRectF rect;bool removed=false;};
    void reset(int level=1,quint32 seed=1);
    bool exposed(int id)const;
    bool pick(int id);
    bool takeReserve(int position);
    bool undo();bool shuffle();bool moveToReserve();
    bool canUndo()const{return m_hasUndo&&!m_undoUsed&&!m_won;}
    bool canShuffle()const;
    bool canReserve()const{return !m_reserveUsed&&m_reserve.isEmpty()&&m_tray.size()>=3&&!m_won;}
    const QVector<Tile>& tiles()const{return m_tiles;}
    const QVector<int>& tray()const{return m_tray;}
    const QVector<int>& reserve()const{return m_reserve;}
    const QVector<int>& solution()const{return m_solution;}
    int remaining()const;int level()const{return m_level;}int moves()const{return m_moves;}
    bool won()const{return m_won;}bool lost()const{return m_lost;}
private:
    void saveUndo();void place(int id);void evaluate();
    QVector<Tile> m_tiles,m_beforeTiles;QVector<int> m_tray,m_reserve,m_solution,m_beforeTray,m_beforeReserve;
    std::mt19937 m_rng{1};int m_level=1,m_moves=0,m_beforeMoves=0;
    bool m_won=false,m_lost=false,m_hasUndo=false,m_undoUsed=false,m_shuffleUsed=false,m_reserveUsed=false;
};
}
