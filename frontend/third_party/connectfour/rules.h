// Ported from devshareacademy/connect-four src/utils.ts, MIT.
// Copyright (c) 2023 Dev Share Academy. See LICENSE and SOURCE.json.
#pragma once
#include <array>
namespace ConnectFourRules {
using Board = std::array<int, 225>;
inline int dropRow(const Board& board, int col) {
    if (col < 0 || col >= 7) return -1;
    for (int row = 5; row >= 0; --row)
        if (!board[row * 7 + col]) return row;
    return -1;
}
// The upstream horizontal, vertical and diagonal four-cell windows are
// combined here into four directions; board storage and player IDs are kept.
inline bool won(const Board& board, int col, int row) {
    const int player = board[row * 7 + col];
    if (!player) return false;
    for (auto direction : std::array<std::array<int, 2>, 4>{{{1,0},{0,1},{1,1},{1,-1}}})
        for (int start = -3; start <= 0; ++start) {
            bool match = true;
            for (int i = 0; i < 4; ++i) {
                int x = col + (start + i) * direction[0], y = row + (start + i) * direction[1];
                if (x < 0 || x >= 7 || y < 0 || y >= 6 || board[y * 7 + x] != player) { match = false; break; }
            }
            if (match) return true;
        }
    return false;
}
}
