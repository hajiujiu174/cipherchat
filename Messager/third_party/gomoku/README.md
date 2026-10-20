# Gomoku rules attribution

Source: https://github.com/youwuyou/gomoku
Pinned commit: `7e831ceadbf97722148082e79f6fe9c6212cc1cc`
License: MIT, Copyright (c) 2023 youwuyou (full text in LICENSE).

`rules.h` adapts `check_win_condition` and `count_stones_one_direction` from
`src/common/game_state/game_state.cpp`. Changes replace the serialized board
object with a 225-cell array, use signed coordinates and make the functions
inline inside a namespace. The 15×15 freestyle rule (five or more wins) is retained.

`upstream_game_state.cpp.txt` is an uncompiled source snapshot for provenance.
The upstream wxWidgets UI, server, AI, and third-party submodules are not bundled
or executed. CipherChat supplies its own Qt board and encrypted chat integration.
