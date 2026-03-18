-- 0001_users.sql
--
-- 作用：
-- 建立认证域中的用户账号表 users。
--
-- 这张表当前承担的职责只有三类：
-- 1. 保存登录账号和基础资料；
-- 2. 保存密码哈希与密码算法；
-- 3. 保存账号状态，支撑后续禁用、锁定等控制。
--
-- 明确不在这张表里做的事：
-- 1. 不存 access token / refresh token；
-- 2. 不存设备级登录状态；
-- 3. 不提前引入好友、会话、消息等其它领域字段。
--
-- 设计约束：
-- - user_id 是系统内外都可稳定引用的业务主键，例如 u_1001；
-- - account 是登录标识，第一版先统一抽象为一个字段；
-- - account 按原值比较，大小写敏感；
-- - password_hash 只保存哈希，不保存明文；
-- - password_algo 预留算法升级空间，避免以后改表；
-- - account_status 为账号治理预留状态位。

CREATE TABLE users (
    -- 对外稳定暴露的用户主键。
    user_id VARCHAR(32) PRIMARY KEY,

    -- 登录账号。
    -- 当前不区分手机号、邮箱、用户名，统一由业务层解释 account 的具体规则。
    account VARCHAR(64) NOT NULL,

    -- 展示昵称。
    -- 注册成功后可以直接返回给客户端使用。
    nickname VARCHAR(64) NOT NULL,

    -- 头像存储 key。
    -- 当前字段名仍沿用 avatar_url，但实际保存的是文件存储层的 storage key，
    -- 例如 avatars/2026/03/16/avatar_xxx.png。
    avatar_url TEXT,

    -- 密码哈希。
    -- 这里只保存哈希结果，绝对不保存明文密码。
    password_hash TEXT NOT NULL,

    -- 密码哈希算法。
    -- 第一版默认 bcrypt，同时为后续迁移到 argon2id 预留兼容空间。
    password_algo VARCHAR(16) NOT NULL DEFAULT 'bcrypt',

    -- 账号状态。
    -- active: 正常可登录
    -- disabled: 管理侧禁用
    -- locked: 风控或安全策略锁定
    account_status VARCHAR(16) NOT NULL DEFAULT 'active',

    -- 最近一次登录成功时间。
    -- 登录完成后由业务层更新，可用于设备管理和安全分析。
    last_login_at TIMESTAMPTZ,

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- 防止业务层传入空白字符串，避免“看起来有值，实际上不可用”的脏数据。
    CONSTRAINT ck_users_user_id_not_blank
        CHECK (btrim(user_id) <> ''),
    CONSTRAINT ck_users_account_not_blank
        CHECK (btrim(account) <> ''),
    CONSTRAINT ck_users_nickname_not_blank
        CHECK (btrim(nickname) <> ''),
    CONSTRAINT ck_users_password_hash_not_blank
        CHECK (btrim(password_hash) <> ''),

    -- 密码算法只允许明确白名单，避免业务层写入拼写错误或未知值。
    CONSTRAINT ck_users_password_algo
        CHECK (password_algo IN ('bcrypt', 'argon2id')),

    -- 账号状态也收敛到有限枚举，便于后续在服务层做明确分支处理。
    CONSTRAINT ck_users_account_status
        CHECK (account_status IN ('active', 'disabled', 'locked'))
);

-- 登录账号大小写敏感。
-- 因此唯一性直接基于 account 原值本身，而不是 lower(account)。
-- 这样 Alice 和 alice 会被视为两个不同账号。
CREATE UNIQUE INDEX ux_users_account
    ON users (account);

-- account_status 的常见用途：
-- 1. 登录时快速判断账号是否允许通过；
-- 2. 后续后台管理按状态筛选账号；
-- 3. 安全治理或风控任务按状态扫描用户。
CREATE INDEX ix_users_account_status
    ON users (account_status);

-- users.updated_at 自动维护。
-- 任何对 users 的 UPDATE 都会先触发 set_updated_at()，统一刷新更新时间。
CREATE TRIGGER trg_users_set_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

-- 元数据注释：
-- 方便后续用 \d+ users 或数据库图形工具直接看到字段职责。
COMMENT ON TABLE users IS
    '用户账号表，支撑注册、登录和基础资料返回。';
COMMENT ON COLUMN users.user_id IS
    '对外稳定暴露的用户标识，例如 u_1001。';
COMMENT ON COLUMN users.account IS
    '登录账号，第一版统一使用一个通用 account 字段，按原值比较且大小写敏感。';
COMMENT ON COLUMN users.nickname IS
    '展示昵称，注册成功后直接返回给客户端。';
COMMENT ON COLUMN users.avatar_url IS
    '头像 storage key，当前字段名沿用 avatar_url，但实际保存的是文件存储定位 key。';
COMMENT ON COLUMN users.password_hash IS
    '密码哈希，不保存明文密码。';
COMMENT ON COLUMN users.password_algo IS
    '密码哈希算法，第一版默认 bcrypt。';
COMMENT ON COLUMN users.account_status IS
    '账号状态：active / disabled / locked。';
COMMENT ON COLUMN users.last_login_at IS
    '最近一次登录成功时间。';
