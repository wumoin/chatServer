-- 0005_conversations.sql
--
-- 作用：
-- 建立聊天域中的会话主表 conversations。
--
-- 这张表当前承担的职责只有三类：
-- 1. 定义“系统里有哪些聊天会话”；
-- 2. 统一承接私聊与后续群聊，而不是为不同聊天形态各建一套表；
-- 3. 为会话列表、历史消息、已读位置和实时消息提供统一归属对象。
--
-- 为什么需要单独一张会话表，而不是让消息直接依赖 sender_id + receiver_id：
-- 1. 私聊和群聊都需要一个稳定的 conversation_id；
-- 2. 会话列表、未读、置顶、草稿等状态都应该挂在“会话”上，而不是直接挂在用户关系上；
-- 3. 统一成会话模型后，后续群聊、系统会话、客服会话都能沿着同一套结构扩展。
--
-- 当前第一版设计重点：
-- - 先把 direct 私聊建模出来；
-- - direct_pair_key 用于保证同一对用户只会生成一条私聊会话；
-- - last_message_seq / last_message_at 为后续会话列表排序和增量同步预留基础字段。
--
-- 依赖关系：
-- - 这张表依赖 users；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE conversations (
    -- 对外稳定暴露的会话业务主键。
    conversation_id VARCHAR(32) PRIMARY KEY,

    -- 会话类型。
    -- direct: 一对一私聊
    -- group: 群聊
    -- system: 系统会话或通知会话
    conversation_type VARCHAR(16) NOT NULL DEFAULT 'direct',

    -- 创建者用户 ID。
    -- 当前允许为空，是为了避免将来删除用户时被 created_by 反向阻塞。
    created_by VARCHAR(32),

    -- 私聊稳定键。
    -- 只对 direct 会话有意义，通常由两个 user_id 排序后拼成稳定值。
    -- 用它做唯一约束，可以避免同一对好友被创建出多个私聊会话。
    direct_pair_key VARCHAR(96),

    -- 当前会话最后一条消息的顺序号。
    -- 还没有消息时保持为 0；后续会话列表可优先按它判断活跃度。
    last_message_seq BIGINT NOT NULL DEFAULT 0,

    -- 当前会话最后一条消息的时间。
    -- 会话还没有消息时允许为空。
    last_message_at TIMESTAMPTZ,

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT fk_conversations_created_by
        FOREIGN KEY (created_by) REFERENCES users(user_id) ON DELETE SET NULL,

    -- 基础非空白约束。
    CONSTRAINT ck_conversations_id_not_blank
        CHECK (btrim(conversation_id) <> ''),
    CONSTRAINT ck_conversations_created_by_not_blank
        CHECK (created_by IS NULL OR btrim(created_by) <> ''),

    -- 会话类型收敛到有限白名单。
    CONSTRAINT ck_conversations_type
        CHECK (conversation_type IN ('direct', 'group', 'system')),

    -- 最后一条消息顺序号不能为负数。
    CONSTRAINT ck_conversations_last_message_seq
        CHECK (last_message_seq >= 0),

    -- direct 会话必须提供 direct_pair_key；其它类型当前不允许使用它。
    CONSTRAINT ck_conversations_direct_pair_key
        CHECK (
            (conversation_type = 'direct' AND direct_pair_key IS NOT NULL AND btrim(direct_pair_key) <> '') OR
            (conversation_type <> 'direct' AND direct_pair_key IS NULL)
        )
);

-- 同一对用户的私聊会话只允许存在一条。
CREATE UNIQUE INDEX ux_conversations_direct_pair_key
    ON conversations (direct_pair_key)
    WHERE conversation_type = 'direct';

-- 会话列表通常会按最近更新时间排序。
CREATE INDEX ix_conversations_type_updated_at
    ON conversations (conversation_type, updated_at DESC);

-- conversations.updated_at 自动维护。
CREATE TRIGGER trg_conversations_set_updated_at
    BEFORE UPDATE ON conversations
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

COMMENT ON TABLE conversations IS
    '会话主表，统一承接私聊、群聊和系统会话。';
COMMENT ON COLUMN conversations.conversation_id IS
    '对外稳定暴露的会话标识，例如 c_1001。';
COMMENT ON COLUMN conversations.conversation_type IS
    '会话类型：direct / group / system。';
COMMENT ON COLUMN conversations.created_by IS
    '创建这段会话的用户 ID，可为空。';
COMMENT ON COLUMN conversations.direct_pair_key IS
    '私聊稳定键，用于保证同一对用户只会生成一条 direct 会话。';
COMMENT ON COLUMN conversations.last_message_seq IS
    '当前会话最后一条消息的会话内顺序号，没有消息时为 0。';
COMMENT ON COLUMN conversations.last_message_at IS
    '当前会话最后一条消息的时间。';
