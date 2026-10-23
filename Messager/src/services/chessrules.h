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
    int columns() const {return game=="xiangqi"?9:8;}
    int rows() const {return game=="xiangqi"?10:8;}
};
class ChessRules {
public:
    static bool replay(const QString& game,const QJsonArray& moves,ChessState& result);
    static QStringList legalMoves(const ChessState& state);
    static QString encode(const QString& game,int from,int to,const QString& promotion={});
    static int from(const QString& game,const QString& move);
    static int to(const QString& game,const QString& move);
    // Position helpers are also used for rule/perft regression checks.
    static bool chessPosition(const QString& fen,ChessState& result);
    static ChessState xiangqiPosition(const std::array<int,90>& board,int turn);
};
}
