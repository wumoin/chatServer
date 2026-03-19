-- 0004_friendships.sql
--
-- 作用：
-- 建立社交关系域中的正式好友关系表 friendships。
--
-- 这张表当前承担的职责只有两类：
-- 1. 记录“某个用户当前有哪些正式好友”；
-- 2. 为好友列表、好友权限判断、后续私聊入口联动提供稳定关系数据。
--
-- 当前采用“双行单向”建模，而不是一行存一对用户。
-- 也就是说：
-- - A 和 B 成为好友后，会插入两条记录：A -> B、B -> A；
-- - 这样查询“我的好友列表”时只需要按 user_id 查单边记录；
-- - 后续如果要引入单方备注名、单方置顶、单方免打扰，也更容易直接扩展在这张表上。
--
-- 依赖关系：
-- - 这张表依赖 users，因此必须在 users 表之后执行；
-- - 如果好友关系来源于某条申请，可通过 created_by_request_id 反查 friend_requests；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE friendships (
    -- 当前这一行所属的用户。
    user_id VARCHAR(32) NOT NULL,

    -- 当前这一行指向的好友用户。
    friend_user_id VARCHAR(32) NOT NULL,

    -- 建立这段好友关系的来源申请。
    -- 当前允许为空，便于后续兼容系统导入关系或后台直接建关系。
    created_by_request_id VARCHAR(32),

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- 这张表天然以“用户 -> 好友”这组关系作为唯一标识。
    CONSTRAINT pk_friendships
        PRIMARY KEY (user_id, friend_user_id),

    CONSTRAINT fk_friendships_user
        FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT fk_friendships_friend_user
        FOREIGN KEY (friend_user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT fk_friendships_request
        FOREIGN KEY (created_by_request_id) REFERENCES friend_requests(request_id)
            ON DELETE SET NULL,

    -- 基础非空白约束。
    CONSTRAINT ck_friendships_user_not_blank
        CHECK (btrim(user_id) <> ''),
    CONSTRAINT ck_friendships_friend_user_not_blank
        CHECK (btrim(friend_user_id) <> ''),

    -- 不允许用户把自己记成好友。
    CONSTRAINT ck_friendships_user_diff
        CHECK (user_id <> friend_user_id)
);

-- 读取好友列表时，通常会按 user_id 查询，并希望较新的好友关系排在前面。
CREATE INDEX ix_friendships_user_created
    ON friendships (user_id, created_at DESC);

-- 按 friend_user_id 反查时，可用于双向删除好友或做关系回查。
CREATE INDEX ix_friendships_friend_user
    ON friendships (friend_user_id);

-- friendships.updated_at 自动维护。
CREATE TRIGGER trg_friendships_set_updated_at
    BEFORE UPDATE ON friendships
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

COMMENT ON TABLE friendships IS
    '正式好友关系表，当前采用双行单向建模：A->B 与 B->A 各存一条。';
COMMENT ON COLUMN friendships.user_id IS
    '当前这一行所属用户 ID，查询我的好友列表时按它过滤。';
COMMENT ON COLUMN friendships.friend_user_id IS
    '当前这一行指向的好友用户 ID。';
COMMENT ON COLUMN friendships.created_by_request_id IS
    '建立这段好友关系的来源申请 ID，可为空。';
