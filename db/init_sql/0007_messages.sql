-- 0007_messages.sql
--
-- 作用：
-- 建立聊天域中的消息主表 messages。
--
-- 这张表当前承担的职责只有三类：
-- 1. 记录某个会话中的历史消息；
-- 2. 通过 conversation_id + seq 提供稳定的会话内顺序；
-- 3. 为历史消息分页、重连补拉、已读同步提供统一基准。
--
-- 为什么 messages 必须围绕 seq 设计：
-- 1. 历史消息分页应该用 before_seq / after_seq，而不是页码；
-- 2. 已读位置本质上读到的是“会话中的第几条消息”；
-- 3. WebSocket 实时消息到达后，也需要用 seq 做顺序对齐和补拉判断。
--
-- 当前第一版设计重点：
-- - 先支持最小消息类型：text / image / file / system；
-- - 消息内容统一收口到 content_json，避免过早把表结构绑死在纯文本消息上；
-- - client_message_id 预留给客户端幂等和重试去重使用。
--
-- 依赖关系：
-- - 这张表依赖 conversations 和 users；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE messages (
    -- 对外稳定暴露的消息业务主键。
    message_id VARCHAR(32) PRIMARY KEY,

    -- 所属会话。
    conversation_id VARCHAR(32) NOT NULL,

    -- 会话内顺序号。
    -- 每个 conversation_id 内独立递增，从 1 开始。
    seq BIGINT NOT NULL,

    -- 发送者用户 ID。
    sender_id VARCHAR(32) NOT NULL,

    -- 客户端本地消息 ID。
    -- 第一版允许为空；如果客户端传入，则可用于幂等和重试去重。
    client_message_id VARCHAR(64),

    -- 消息类型。
    -- text: 文本消息
    -- image: 图片消息
    -- file: 文件消息
    -- system: 系统消息
    message_type VARCHAR(16) NOT NULL DEFAULT 'text',

    -- 消息内容。
    -- 第一版统一使用 JSONB，便于后续平滑承接图片、文件、系统提示等多形态消息。
    content_json JSONB NOT NULL DEFAULT '{}'::jsonb,

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT fk_messages_conversation
        FOREIGN KEY (conversation_id) REFERENCES conversations(conversation_id)
            ON DELETE CASCADE,
    CONSTRAINT fk_messages_sender
        FOREIGN KEY (sender_id) REFERENCES users(user_id)
            ON DELETE RESTRICT,

    -- 基础非空白约束。
    CONSTRAINT ck_messages_id_not_blank
        CHECK (btrim(message_id) <> ''),
    CONSTRAINT ck_messages_conversation_not_blank
        CHECK (btrim(conversation_id) <> ''),
    CONSTRAINT ck_messages_sender_not_blank
        CHECK (btrim(sender_id) <> ''),
    CONSTRAINT ck_messages_client_message_id_not_blank
        CHECK (client_message_id IS NULL OR btrim(client_message_id) <> ''),

    -- seq 必须是正整数。
    CONSTRAINT ck_messages_seq_positive
        CHECK (seq > 0),

    -- 消息类型收敛到有限白名单。
    CONSTRAINT ck_messages_type
        CHECK (message_type IN ('text', 'image', 'file', 'system')),

    -- content_json 当前统一要求是 JSON object。
    CONSTRAINT ck_messages_content_json_object
        CHECK (jsonb_typeof(content_json) = 'object')
);

-- 历史消息分页、实时补拉和顺序校验都围绕 conversation_id + seq。
CREATE UNIQUE INDEX ux_messages_conversation_seq
    ON messages (conversation_id, seq);

-- 客户端若传入 client_message_id，则在同一会话、同一发送者范围内要求唯一，
-- 便于后续做发送重试去重和 message.ack 对齐。
CREATE UNIQUE INDEX ux_messages_conversation_sender_client_message
    ON messages (conversation_id, sender_id, client_message_id)
    WHERE client_message_id IS NOT NULL;

-- 按发送者回查消息时可用于审计和问题排查。
CREATE INDEX ix_messages_sender_created_at
    ON messages (sender_id, created_at DESC);

-- messages.updated_at 自动维护。
CREATE TRIGGER trg_messages_set_updated_at
    BEFORE UPDATE ON messages
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

COMMENT ON TABLE messages IS
    '消息主表，记录会话内消息内容，并通过 seq 提供稳定顺序。';
COMMENT ON COLUMN messages.message_id IS
    '对外稳定暴露的消息标识，例如 m_1001。';
COMMENT ON COLUMN messages.conversation_id IS
    '所属会话 ID，关联 conversations.conversation_id。';
COMMENT ON COLUMN messages.seq IS
    '会话内顺序号，历史消息分页和已读位置都围绕它工作。';
COMMENT ON COLUMN messages.sender_id IS
    '发送者用户 ID，关联 users.user_id。';
COMMENT ON COLUMN messages.client_message_id IS
    '客户端本地消息 ID，用于幂等和重试去重，可为空。';
COMMENT ON COLUMN messages.message_type IS
    '消息类型：text / image / file / system。';
COMMENT ON COLUMN messages.content_json IS
    '消息内容 JSONB；第一版统一使用对象结构承接不同消息类型。';
