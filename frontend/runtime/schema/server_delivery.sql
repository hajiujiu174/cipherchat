-- M2-03 独立队列表；不修改旧表，迁移时由实现任务明确一次性迁移策略。
CREATE TABLE IF NOT EXISTS pending_deliveries_v2 (
    recipient TEXT NOT NULL,
    message_id TEXT NOT NULL,
    sender TEXT NOT NULL,
    ciphertext_json TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY(recipient, message_id)
);
CREATE INDEX IF NOT EXISTS pending_deliveries_v2_order
ON pending_deliveries_v2(recipient, created_at_ms, message_id);
-- peek SELECT，不删除；authenticatedRecipient+message_id 匹配后 DELETE。
-- 签收事务删除密文，同时只保留绑定和摘要；发送方重传时可恢复签收通知。
CREATE TABLE IF NOT EXISTS delivery_receipts_v2 (
    recipient TEXT NOT NULL,
    message_id TEXT NOT NULL,
    sender TEXT NOT NULL,
    ciphertext_sha256 BLOB NOT NULL,
    PRIMARY KEY(recipient, message_id)
);
