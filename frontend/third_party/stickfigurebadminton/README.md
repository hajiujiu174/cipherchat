# Stick Figure Badminton source and Qt adaptation

Upstream: https://github.com/b-z/StickFigureBadminton

Pinned commit: `9383047c3f62673b41a9ba5c611ad41bd9d156bf`

Authors: 种璐瑶 / 周伯威 / 钱珺, April 2015.

The upstream repository does not declare a license. This is source-available code,
not an MIT-licensed dependency. The user has requested a personal-use adaptation
while accepting that upstream has no open-source license. Preserve this notice
and the upstream author comments; this notice grants no additional rights.

`upstream/main.js` and `upstream/readme.md` are unmodified source references.
The native implementation is in `src/services/badmintonengine.cpp`. It adapts
the 15 ms movement/gravity/drag constants, racket range and hit-vector formulas,
side-wall and net rebound, serve positioning, and first-to-7 scoring. It adds
single-hit-per-side enforcement, swept net collision, bounded host-authoritative
network input and snapshots, native Qt vector graphics, computer practice,
friend invitations, and a separate full-screen window. Upstream music, artwork,
and browser libraries are not bundled or executed.
