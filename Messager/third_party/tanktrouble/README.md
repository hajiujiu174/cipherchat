# TankTrouble Qt port

Upstream: https://github.com/JustDoIt0910/TankTroubleServer
Pinned commit: `9a66f7140063bb9291fcf787e457d05e73d2bd49`
MIT Copyright (c) 2023 赵睿. Full license retained in LICENSE and upstream/LICENSE.

The upstream game/Maze, Object, Tank, Shell, Block, util/Math and util/Vec C++ sources are compiled unchanged. Randomized Prim maze construction, tank control flags and kinematics, shell ammunition, bounce-angle helpers, rotated rectangle SAT, and rectangle/circle collision mathematics are reused.

CipherChat's tankengine.cpp replaces the upstream server GameRoom orchestrator: fixed 20 ms steps, one-pixel shell substeps, bounded lifetime, simultaneous-death draws, five-point matches and timed rounds. Tankcontroller implements host-authoritative snapshots and guest inputs over the authenticated TLS game_relay channel. TankPage supplies Qt painting and controls. GTK, ev, tinyorm and upstream networking are not included at runtime. This is an open-source recreation of the gameplay, not the official 4399 game.

The Qt adapter also supports host-validated mouse targets: turn before advancing, at most 2 arena units per 20 ms step (100 units/s), with existing SAT collision checks. Pointer events and incoming packets do not step the simulation; the upstream game sources remain unchanged.
