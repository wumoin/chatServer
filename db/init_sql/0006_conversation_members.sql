-- 0006_conversation_members.sql
--
-- 作用：
-- 建立聊天域中的会话成员关系表 conversation_members。
--
-- 这张表当前承担的职责只有三类：
-- 1. 记录“哪些用户属于哪个会话”；
-- 2. 记录用户在该会话里的成员级状态，例如角色和 last_read_seq；
-- 3. 为会话列表过滤、消息权限校验和已读同步提供稳定数据来源。
--
-- 为什么成员关系要单独建表：
-- 1. conversations 只定义“有哪段聊天”，不负责保存成员列表；
-- 2. 已读位置属于“某个用户在某个会话里的状态”，天然应该挂在成员表；
-- 3. 后续群聊时，成员角色、禁言、加入时间都需要围绕这张表扩展。
--
-- 当前第一版设计重点：
-- - 私聊会在这里落两条记录：A -> conversation、B -> conversation；
-- - last_read_seq 以用户维度保存，便于多端同步时围绕统一游标工作；
-- - member_role 先为群聊扩展预留，但 direct 私聊第一版统一可用 member。
--
-- 依赖关系：
-- - 这张表依赖 conversations 和 users；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE conversation_members (
    -- 所属会话。
    conversation_id VARCHAR(32) NOT NULL,

    -- 所属用户。
    user_id VARCHAR(32) NOT NULL,

    -- 成员角色。
    -- member: 普通成员
    -- owner: 群主
    -- admin: 管理员
    -- system: 系统成员或保留角色
    member_role VARCHAR(16) NOT NULL DEFAULT 'member',

    -- 加入会话的时间。
    joined_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- 当前用户在该会话中最后已读到的消息顺序号。
    -- 0 表示还没有读到任何消息。
    last_read_seq BIGINT NOT NULL DEFAULT 0,

    -- 最近一次推进 last_read_seq 的时间。
    -- 第一版允许为空，避免把已读推进时机过早写死。
    last_read_at TIMESTAMPTZ,

    -- 标准更新时间字段。
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT pk_conversation_members
        PRIMARY KEY (conversation_id, user_id),

    CONSTRAINT fk_conversation_members_conversation
        FOREIGN KEY (conversation_id) REFERENCES conversations(conversation_id)
            ON DELETE CASCADE,
    CONSTRAINT fk_conversation_members_user
        FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,

    -- 基础非空白约束。
    CONSTRAINT ck_conversation_members_conversation_not_blank
        CHECK (btrim(conversation_id) <> ''),
    CONSTRAINT ck_conversation_members_user_not_blank
        CHECK (btrim(user_id) <> ''),

    -- 成员角色收敛到有限白名单。
    CONSTRAINT ck_conversation_members_role
        CHECK (member_role IN ('member', 'owner', 'admin', 'system')),

    -- 已读位置不能小于 0。
    CONSTRAINT ck_conversation_members_last_read_seq
        CHECK (last_read_seq >= 0)
);

-- 查询“我参与的会话列表”时，通常先按 user_id 过滤。
CREATE INDEX ix_conversation_members_user_updated_at
    ON conversation_members (user_id, updated_at DESC);

-- 按 conversation_id 读取成员列表时可直接命中主键前缀，但这里保留显式索引，
-- 便于后续成员清单、权限校验和统计场景更直观。
CREATE INDEX ix_conversation_members_conversation_role
    ON conversation_members (conversation_id, member_role);

-- conversation_members.updated_at 自动维护。
CREATE TRIGGER trg_conversation_members_set_updated_at
    BEFORE UPDATE ON conversation_members
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

COMMENT ON TABLE conversation_members IS
    '会话成员关系表，记录成员身份、加入时间和 last_read_seq。';
COMMENT ON COLUMN conversation_members.conversation_id IS
    '所属会话 ID，关联 conversations.conversation_id。';
COMMENT ON COLUMN conversation_members.user_id IS
    '所属用户 ID，关联 users.user_id。';
COMMENT ON COLUMN conversation_members.member_role IS
    '成员角色：member / owner / admin / system。';
COMMENT ON COLUMN conversation_members.joined_at IS
    '加入会话的时间。';
COMMENT ON COLUMN conversation_members.last_read_seq IS
    '当前用户在该会话中最后已读到的消息顺序号。';
COMMENT ON COLUMN conversation_members.last_read_at IS
    '最近一次推进 last_read_seq 的时间。';
