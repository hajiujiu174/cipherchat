#pragma once
#include <QJsonArray>
#include <QStringList>
#include <array>
namespace Messager {
struct ChessState {
    QString game="chess",fen,reason;
    std::array<int,90> board{};
    QStringList moves;
    int turn=1,winner=0;
    bool check=false;
    // 返回游戏联机使用的列数。
    int columns() const {return game=="xiangqi"?9:8;}
    // 返回游戏联机使用的行数。
    int rows() const {return game=="xiangqi"?10:8;}
};
class ChessRules {
public:
    // 重放游戏联机。
    static bool replay(const QString& game,const QJsonArray& moves,ChessState& result);
    // 返回游戏联机使用的可用走法。
    static QStringList legalMoves(const ChessState& state);
    // 编码游戏联机。
    static QString encode(const QString& game,int from,int to,const QString& promotion={});
    // 将游戏联机转换为调用方使用的表示。
    static int from(const QString& game,const QString& move);
    // 将游戏联机转换为调用方使用的表示。
    static int to(const QString& game,const QString& move);
    // Position helpers are also used for rule/perft regression checks.
    // 返回游戏联机使用的chess位置。
    static bool chessPosition(const QString& fen,ChessState& result);
    // 返回游戏联机使用的xiangqi位置。
    static ChessState xiangqiPosition(const std::array<int,90>& board,int turn);
};
}
