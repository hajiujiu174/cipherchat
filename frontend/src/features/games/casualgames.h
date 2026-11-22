#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <random>
namespace Messager {
class JumpGame {
public:
    struct Platform {QPointF center;double rx=42,ry=25;int color=0;};
    // 重置游戏联机。
    void reset(quint32 seed=1);
    // 返回游戏联机使用的跳跃。
    bool jump(int heldMs);
    // 返回游戏联机使用的当前。
    const Platform& current()const{return m_current;}
    // 返回游戏联机使用的下一项。
    const Platform& next()const{return m_next;}
    // 返回当前棋子或跳跃角色的位置。
    QPointF pawn()const{return m_pawn;}
    // 返回最近一次跳跃的落点。
    QPointF lastLanding()const{return m_landing;}
    // 返回当前游戏分数。
    int score()const{return m_score;}
    // 报告当前游戏是否已进入失败状态。
    bool lost()const{return m_lost;}
    // 报告最近一次跳跃是否为精准落地。
    bool perfect()const{return m_perfect;}
    // 返回游戏联机使用的距离。
    double distance()const;
    static constexpr double ChargeSpeed=.28;
    static constexpr int MaxChargeMs=1200;
private:
    // 生成游戏联机。
    void generate();
    std::mt19937 m_rng{1};Platform m_current,m_next;QPointF m_pawn,m_landing;
    int m_score=0;bool m_lost=false,m_perfect=false;
};
class SheepGame {
public:
    struct Tile {int id=0,kind=0,layer=0;QRectF rect;bool removed=false;};
    // 重置游戏联机。
    void reset(int level=1,quint32 seed=1);
    // 判断指定牌块是否处于可选择的暴露位置。
    bool exposed(int id)const;
    // 选取游戏联机。
    bool pick(int id);
    // 取出暂存区。
    bool takeReserve(int position);
    // 撤销游戏联机。
    bool undo();/* 洗牌游戏联机。 */ bool shuffle();/* 移动To暂存区。 */ bool moveToReserve();
    // 判断游戏联机当前是否允许撤销。
    bool canUndo()const{return m_hasUndo&&!m_undoUsed&&!m_won;}
    // 判断游戏联机当前是否允许Shuffle。
    bool canShuffle()const;
    // 判断游戏联机当前是否允许暂存区。
    bool canReserve()const{return !m_reserveUsed&&m_reserve.isEmpty()&&m_tray.size()>=3&&!m_won;}
    // 返回关卡中的牌块列表。
    const QVector<Tile>& tiles()const{return m_tiles;}
    // 返回游戏联机使用的托盘。
    const QVector<int>& tray()const{return m_tray;}
    // 返回游戏联机使用的暂存区。
    const QVector<int>& reserve()const{return m_reserve;}
    // 返回生成牌局时保存的可解顺序。
    const QVector<int>& solution()const{return m_solution;}
    // 返回游戏联机的尚未处理的数量。
    int remaining()const;/* 返回游戏联机使用的关卡。 */ int level()const{return m_level;}/* 返回游戏联机使用的走法。 */ int moves()const{return m_moves;}
    // 报告当前游戏是否已经胜利。
    bool won()const{return m_won;}/* 报告当前游戏是否已进入失败状态。 */ bool lost()const{return m_lost;}
private:
    // 保存撤销。
    void saveUndo();/* 将选中的牌块放入托盘，供后续消除判断。 */ void place(int id);/* 求值游戏联机。 */ void evaluate();
    QVector<Tile> m_tiles,m_beforeTiles;QVector<int> m_tray,m_reserve,m_solution,m_beforeTray,m_beforeReserve;
    std::mt19937 m_rng{1};int m_level=1,m_moves=0,m_beforeMoves=0;
    bool m_won=false,m_lost=false,m_hasUndo=false,m_undoUsed=false,m_shuffleUsed=false,m_reserveUsed=false;
};
}
