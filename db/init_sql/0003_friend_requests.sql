-- 0003_friend_requests.sql
--
-- 作用：
-- 建立社交关系域中的好友申请表 friend_requests。
--
-- 这张表当前承担的职责只有三类：
-- 1. 记录“谁向谁发起了好友申请”；
-- 2. 记录申请当前状态，例如 pending / accepted / rejected；
-- 3. 为后续“新的朋友”“我发出的申请”“接受/拒绝申请”提供稳定数据来源。
--
-- 为什么不能把好友申请和最终好友关系混成一张表：
-- 1. 好友申请是流程数据，会经历 pending -> accepted / rejected / canceled；
-- 2. 好友关系是结果数据，一旦建立后生命周期更稳定；
-- 3. 如果混成一张表，查询好友列表时必须永远带状态过滤，历史申请也会污染关系数据。
--
-- 依赖关系：
-- - 这张表依赖 users，因此必须在 users 表之后执行；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE friend_requests (
    -- 对外稳定暴露的好友申请主键。
    request_id VARCHAR(32) PRIMARY KEY,

    -- 发起申请的用户。
    requester_id VARCHAR(32) NOT NULL,

    -- 接收申请的目标用户。
    target_id VARCHAR(32) NOT NULL,

    -- 申请附言。
    -- 当前允许为空，但如果传入则限制长度，避免无上限文本污染列表页。
    request_message VARCHAR(200),

    -- 申请状态。
    -- pending: 待处理
    -- accepted: 已接受
    -- rejected: 已拒绝
    -- canceled: 发起方主动撤回
    -- expired: 后台超时失效
    status VARCHAR(16) NOT NULL DEFAULT 'pending',

    -- 申请被处理的时间。
    -- 只要状态不再是 pending，就必须补上 handled_at。
    handled_at TIMESTAMPTZ,

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT fk_friend_requests_requester
        FOREIGN KEY (requester_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT fk_friend_requests_target
        FOREIGN KEY (target_id) REFERENCES users(user_id) ON DELETE CASCADE,

    -- 防止业务层传入逻辑上无效的空字符串。
    CONSTRAINT ck_friend_requests_id_not_blank
        CHECK (btrim(request_id) <> ''),
    CONSTRAINT ck_friend_requests_requester_not_blank
        CHECK (btrim(requester_id) <> ''),
    CONSTRAINT ck_friend_requests_target_not_blank
        CHECK (btrim(target_id) <> ''),

    -- 不允许自己给自己发好友申请。
    CONSTRAINT ck_friend_requests_requester_target_diff
        CHECK (requester_id <> target_id),

    -- 申请状态收敛到固定白名单。
    CONSTRAINT ck_friend_requests_status
        CHECK (status IN ('pending', 'accepted', 'rejected', 'canceled', 'expired')),

    -- 处理中的申请不应有 handled_at；非 pending 状态必须记录处理时间。
    CONSTRAINT ck_friend_requests_handled_at
        CHECK (
            (status = 'pending' AND handled_at IS NULL) OR
            (status <> 'pending' AND handled_at IS NOT NULL)
        )
);

-- 读取“我收到的申请”时，通常会按 target_id + status 查询，并希望新的申请排在前面。
CREATE INDEX ix_friend_requests_target_status_created
    ON friend_requests (target_id, status, created_at DESC);

-- 读取“我发出的申请”时，通常会按 requester_id + status 查询。
CREATE INDEX ix_friend_requests_requester_status_created
    ON friend_requests (requester_id, status, created_at DESC);

-- 同一对用户在任意方向上，同时只允许存在一条 pending 申请。
-- 这样可以避免 A->B 和 B->A 都挂着待处理申请，导致前后端逻辑混乱。
CREATE UNIQUE INDEX ux_friend_requests_pair_pending
    ON friend_requests (
        LEAST(requester_id, target_id),
        GREATEST(requester_id, target_id)
    )
    WHERE status = 'pending';

-- friend_requests.updated_at 自动维护。
CREATE TRIGGER trg_friend_requests_set_updated_at
    BEFORE UPDATE ON friend_requests
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

COMMENT ON TABLE friend_requests IS
    '好友申请流程表，记录 pending / accepted / rejected 等状态流转。';
COMMENT ON COLUMN friend_requests.request_id IS
    '对外稳定暴露的好友申请标识，例如 fr_1001。';
COMMENT ON COLUMN friend_requests.requester_id IS
    '发起申请的用户 ID，关联 users.user_id。';
COMMENT ON COLUMN friend_requests.target_id IS
    '接收申请的目标用户 ID，关联 users.user_id。';
COMMENT ON COLUMN friend_requests.request_message IS
    '好友申请附言，当前限制最长 200 个字符。';
COMMENT ON COLUMN friend_requests.status IS
    '好友申请状态：pending / accepted / rejected / canceled / expired。';
COMMENT ON COLUMN friend_requests.handled_at IS
    '申请被接受、拒绝、撤回或过期的时间。';
