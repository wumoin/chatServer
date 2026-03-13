-- 0002_device_sessions.sql
--
-- 作用：
-- 建立认证域中的设备级登录会话表 device_sessions。
--
-- 为什么要单独建这张表，而不是把 refresh token 直接挂在 users 上：
-- 1. 一个用户可能同时在多个设备登录；
-- 2. refresh token 天然属于“设备会话”，不是“账号主体”；
-- 3. 主动登出、被踢下线、同设备重复登录，这些都是设备粒度事件。
--
-- 这张表当前支撑的核心场景：
-- 1. 登录成功后创建一条新的设备会话；
-- 2. refresh token 校验和轮换；
-- 3. 设备管理页展示登录设备；
-- 4. 主动登出、过期失效、被踢下线等状态流转。
--
-- 依赖关系：
-- - 这张表依赖 users，因此必须在 users 表之后执行；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE device_sessions (
    -- 对外稳定暴露的设备会话主键。
    device_session_id VARCHAR(32) PRIMARY KEY,

    -- 所属用户。
    -- 通过外键绑定到 users.user_id，保证孤儿会话不会出现。
    user_id VARCHAR(32) NOT NULL,

    -- 客户端设备唯一标识。
    -- 由客户端持久化生成，用于识别“是不是同一台设备”。
    device_id VARCHAR(128) NOT NULL,

    -- 设备平台。
    -- 例如 desktop / android / ios，便于后续做平台维度风控和展示。
    device_platform VARCHAR(32) NOT NULL,

    -- 设备名称。
    -- 例如 “MacBook Pro” 或 “Windows Desktop”，主要用于设备管理页展示。
    device_name VARCHAR(128),

    -- 客户端版本号。
    -- 便于后续兼容性排查和灰度策略统计。
    client_version VARCHAR(32),

    -- 登录来源 IP。
    login_ip INET,

    -- 登录时上报的 User-Agent 或等价客户端描述。
    login_user_agent TEXT,

    -- refresh token 的哈希值。
    -- 数据库只存哈希，不存原始 refresh token。
    refresh_token_hash TEXT NOT NULL,

    -- 当前 refresh token 的过期时间。
    refresh_token_expires_at TIMESTAMPTZ NOT NULL,

    -- 最近一次活跃时间。
    -- 后续既可以给设备页展示，也可以辅助判断是否在线。
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- 设备会话状态。
    -- active: 当前有效
    -- revoked: 主动登出或后台失效
    -- expired: refresh token 过期
    -- kicked: 被其它登录或管理动作踢下线
    session_status VARCHAR(16) NOT NULL DEFAULT 'active',

    -- 会话失效时间。
    revoked_at TIMESTAMPTZ,

    -- 会话失效原因。
    -- 例如 logout / refresh_expired / kicked。
    revoke_reason VARCHAR(64),

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT fk_device_sessions_user
        FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,

    -- 基础非空白约束，防止出现“逻辑上无效但数据库不报错”的脏值。
    CONSTRAINT ck_device_sessions_id_not_blank
        CHECK (btrim(device_session_id) <> ''),
    CONSTRAINT ck_device_sessions_user_id_not_blank
        CHECK (btrim(user_id) <> ''),
    CONSTRAINT ck_device_sessions_device_id_not_blank
        CHECK (btrim(device_id) <> ''),
    CONSTRAINT ck_device_sessions_platform_not_blank
        CHECK (btrim(device_platform) <> ''),
    CONSTRAINT ck_device_sessions_refresh_token_hash_not_blank
        CHECK (btrim(refresh_token_hash) <> ''),

    -- 会话状态只允许有限白名单。
    CONSTRAINT ck_device_sessions_status
        CHECK (session_status IN ('active', 'revoked', 'expired', 'kicked')),

    -- 状态和 revoked_at 要保持一致：
    -- - active 时不允许有 revoked_at；
    -- - 非 active 时必须补 revoked_at。
    CONSTRAINT ck_device_sessions_revoked_at
        CHECK (
            (session_status = 'active' AND revoked_at IS NULL) OR
            (session_status <> 'active' AND revoked_at IS NOT NULL)
        )
);

-- 常见读取路径：
-- 1. 登录成功后按 user_id + device_id 判断同设备是否已有活跃会话；
-- 2. refresh / 设备管理页按 user_id 列出用户的设备会话；
-- 3. 设备列表通常希望最近活跃的设备排在前面。
CREATE INDEX ix_device_sessions_user_status
    ON device_sessions (user_id, session_status, last_seen_at DESC);

-- 同一用户在同一 device_id 上，只允许保留一条 active 会话。
-- 如果同设备重复登录，服务端应先失效旧会话，再创建新会话。
CREATE UNIQUE INDEX ux_device_sessions_user_device_active
    ON device_sessions (user_id, device_id)
    WHERE session_status = 'active';

-- 清理过期 refresh token 的后台任务，通常会按状态和过期时间扫描。
CREATE INDEX ix_device_sessions_status_expires_at
    ON device_sessions (session_status, refresh_token_expires_at);

-- device_sessions.updated_at 自动维护。
CREATE TRIGGER trg_device_sessions_set_updated_at
    BEFORE UPDATE ON device_sessions
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- 元数据注释：
-- 方便后续直接从数据库对象查看设计意图。
COMMENT ON TABLE device_sessions IS
    '设备级登录会话表，支撑 refresh token、多端登录和设备级失效控制。';
COMMENT ON COLUMN device_sessions.device_session_id IS
    '对外稳定暴露的设备会话标识，例如 ds_7001。';
COMMENT ON COLUMN device_sessions.user_id IS
    '所属用户 ID，关联 users.user_id。';
COMMENT ON COLUMN device_sessions.device_id IS
    '客户端设备唯一标识，由客户端持久化生成。';
COMMENT ON COLUMN device_sessions.device_platform IS
    '设备平台，例如 desktop / android / ios。';
COMMENT ON COLUMN device_sessions.device_name IS
    '设备名称，便于后续设备管理页展示。';
COMMENT ON COLUMN device_sessions.client_version IS
    '客户端版本号，便于排查兼容性问题。';
COMMENT ON COLUMN device_sessions.login_ip IS
    '本次登录时记录的客户端 IP。';
COMMENT ON COLUMN device_sessions.login_user_agent IS
    '本次登录时记录的 User-Agent 或等价客户端描述。';
COMMENT ON COLUMN device_sessions.refresh_token_hash IS
    '当前有效 refresh token 的哈希值。';
COMMENT ON COLUMN device_sessions.refresh_token_expires_at IS
    '当前 refresh token 的过期时间。';
COMMENT ON COLUMN device_sessions.last_seen_at IS
    '最近一次活跃时间，可用于设备列表和在线状态辅助判断。';
COMMENT ON COLUMN device_sessions.session_status IS
    '设备会话状态：active / revoked / expired / kicked。';
COMMENT ON COLUMN device_sessions.revoked_at IS
    '会话被主动失效或强制下线的时间。';
COMMENT ON COLUMN device_sessions.revoke_reason IS
    '会话失效原因，例如 logout / refresh_expired / kicked。';
