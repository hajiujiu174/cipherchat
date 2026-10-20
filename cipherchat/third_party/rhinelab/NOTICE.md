# RhineLabUI attribution

Upstream: https://github.com/LBEILC/RhineLabUI
Revision: ee5779741c6c0c916e416705fa634c7abf905c73 (retrieved 2026-09-27).
Copyright (c) 2026 LBEILC. See LICENSE (MIT).

Unmodified assets: public/assets/archive-cassette.glb (SHA-256 dda42b9b3b471d11c69820a64084d254a64b27761287ab0e35dd8387ec0a0bed), src/motion.ts.
Adaptations: framework/gui/rhine/Motion.js ports the smooth, baselineSelectionWave, idleWave and damp functions; ArchiveView.qml and rhine_theme.hpp adapt the archive composition and src/style.css palette, typography and hairline navigation for CipherChat.

The Qt renderer uses the original cassette mesh for its foreground model and simplified instances for the background array. Its lighting/transmission differ from Three.js. CipherChat uses its own account verification and communications; no upstream archive lore, fictional authentication, sampled audio, fonts or PWA code is included.

The upstream license excludes third-party Arknights names, marks, settings and original visual-design rights; those rights remain with their respective owners. The original model may contain such elements. This port is not affiliated with the original game's rightsholders.

The native runtime additionally uses Qt Quick / Qt Quick 3D. Qt module licenses and the Qt third-party notices must accompany a distributed package; Qt Quick 3D is available under GPL-3.0 or a commercial Qt license. The port sources are in framework/gui/rhine and framework/gui/rhine_theme.hpp.
