# connectfour rule port

Source: https://github.com/devshareacademy/connect-four

Pinned commit: `590702b55a0a0f261ce171a78d878985e0e73656`

License: MIT, full text retained in LICENSE.

The Connect Four drop-row and four-cell window rules in src/utils.ts are ported to C++17. Horizontal/vertical/diagonal windows share one direction loop. The Qt board, chat invitations and room synchronization are supplied by CipherChat.

The upstream_*.txt files retain the uncompiled source snapshots used by this port. No npm, Cinder, upstream server or external game website is required at runtime.
