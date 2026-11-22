# chess-library

Source: https://github.com/Disservin/chess-library

Pinned commit: `53e6a841dcda7059a2af363d85f785ef1817304a`

MIT license: see LICENSE. `chess.hpp` is retained unchanged.
The Qt adapter is in `src/services/chessrules.cpp`, including legal move generation,
checkmate/stalemate, castling, en passant, promotion and draw detection.
Casual rooms automatically award the claimable threefold and fifty-move draws.
This library is used only for international chess. Xiangqi is implemented natively
in the same adapter, with the casual repetition policy displayed in the game UI.
