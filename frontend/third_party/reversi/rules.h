// Adapted from Kaahan Motwani's Cinder-Othello logic.cc (MIT).
// Copyright (c) 2020 Kaahan Motwani. See LICENSE and SOURCE.json.
#pragma once
#include <array>
#include <vector>
namespace ReversiRules {
using Board = std::array<int, 225>;
inline bool inBounds(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }
inline std::vector<int> flips(const Board& board, int x, int y, int player) {
    std::vector<int> result;
    if (!inBounds(x,y) || board[y*8+x]) return result;
    // Port of IsMoveValid / FlipPieces: collect opponent pieces in each of
    // eight directions, committing only when bounded by a piece of our colour.
    for (int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy) {
        if (!dx && !dy) continue;
        int col=x+dx,row=y+dy;
        std::vector<int> line;
        while (inBounds(col,row) && board[row*8+col] == 3-player) {
            line.push_back(row*8+col);col+=dx;row+=dy;
        }
        if (!line.empty() && inBounds(col,row) && board[row*8+col] == player)
            result.insert(result.end(),line.begin(),line.end());
    }
    return result;
}
inline bool hasMove(const Board& board, int player) {
    for(int y=0;y<8;++y) for(int x=0;x<8;++x)
        if (!flips(board,x,y,player).empty()) return true;
    return false;
}
inline bool play(Board& board, int x, int y, int player) {
    auto captured=flips(board,x,y,player);
    if (captured.empty()) return false;
    board[y*8+x]=player;
    for(int index:captured) board[index]=player;
    return true;
}
}
