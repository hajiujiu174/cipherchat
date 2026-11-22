// Adapted from youwuyou/gomoku, game_state.cpp check_win_condition and
// count_stones_one_direction, commit 7e831ceadbf97722148082e79f6fe9c6212cc1cc.
// Copyright (c) 2023 youwuyou. MIT License; see LICENSE in this directory.
#pragma once
#include <array>
#include <vector>
namespace GomokuRules {
inline unsigned count(const std::array<int,225>& board, int x, int y, int dx, int dy, int colour) {
    if ((x == 0 && dx == -1) || (y == 0 && dy == -1) || (x == 14 && dx == 1) || (y == 14 && dy == 1)) return 0;
    if (board[(y + dy)*15 + x + dx] != colour) return 0;
    return 1 + count(board, x + dx, y + dy, dx, dy, colour);
}
inline bool won(const std::array<int,225>& board, int x, int y, int colour) {
    std::vector<unsigned> directions;
    for (int i=-1;i<2;++i) for(int j=-1;j<2;++j) if(i || j) directions.push_back(count(board,x,y,i,j,colour));
    return directions[0]+directions[7]+1>=5 || directions[1]+directions[6]+1>=5 ||
           directions[2]+directions[5]+1>=5 || directions[3]+directions[4]+1>=5;
}
}
