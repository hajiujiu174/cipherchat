-- M3-03 的最小课程设计持久层，当前只有结构，尚未执行到用户数据库。
-- 同一数据库仅服务一个 account_scope；快照包含身份/ratchet/history/outbox/transfer。
PRAGMA user_version = 1;
CREATE TABLE IF NOT EXISTS client_state (
    singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
    server_id TEXT NOT NULL,
    username TEXT NOT NULL,
    revision INTEGER NOT NULL CHECK(revision >= 0),
    format_version INTEGER NOT NULL CHECK(format_version = 1),
    protected_snapshot BLOB NOT NULL
);
-- 初次打开写入 revision=0 的受保护空快照。
-- commit: BEGIN IMMEDIATE -> UPDATE ... WHERE revision=:expected
-- -> 检查 changed rows=1 -> COMMIT，否则 ROLLBACK + Conflict。

