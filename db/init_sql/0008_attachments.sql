-- 0008_attachments.sql
--
-- 作用：
-- 建立聊天域中的附件主表 attachments。
--
-- 这张表当前承担的职责只有三类：
-- 1. 记录聊天过程中上传成功的图片、文件等附件元数据；
-- 2. 把“文件内容存储”与“消息内容引用”拆开，避免消息表直接承载底层存储细节；
-- 3. 为 `POST /api/v1/files/upload` 和 `GET /api/v1/files/{attachment_id}` 提供统一数据来源。
--
-- 为什么附件要单独建表：
-- 1. 上传接口会先于消息发送发生，此时附件需要先有一个稳定业务 ID；
-- 2. 下载接口应该围绕附件实体做权限校验和文件定位，而不是直接暴露 storage_key；
-- 3. 后续若接对象存储、哈希去重、缩略图或审计能力，都更适合扩在附件表上。
--
-- 当前第一版设计重点：
-- - 文件内容继续交给 storage/file_storage.* 管理；
-- - 数据库存储 attachment_id、storage_key、mime_type、尺寸等元数据；
-- - messages.content_json 后续只引用 attachment_id 和展示用字段，不直接暴露绝对路径。
--
-- 依赖关系：
-- - 这张表依赖 users；
-- - 这张表同样依赖 set_updated_at() 触发器函数。

CREATE TABLE attachments (
    -- 对外稳定暴露的附件业务主键。
    attachment_id VARCHAR(32) PRIMARY KEY,

    -- 上传者用户 ID。
    uploader_user_id VARCHAR(32) NOT NULL,

    -- 存储提供者。
    -- local: 本地磁盘
    -- minio: 对象存储（预留）
    storage_provider VARCHAR(16) NOT NULL DEFAULT 'local',

    -- 存储层返回的稳定定位 key。
    -- 例如 files/2026/03/20/file_xxx.png。
    storage_key TEXT NOT NULL,

    -- 原始文件名，供下载和界面展示使用。
    original_file_name TEXT NOT NULL,

    -- MIME 类型。
    mime_type VARCHAR(128) NOT NULL,

    -- 文件字节大小。
    size_bytes BIGINT NOT NULL,

    -- 文件内容摘要，第一版允许为空。
    sha256 VARCHAR(64),

    -- 附件媒体类别。
    -- image: 图片附件
    -- file: 普通文件附件
    media_kind VARCHAR(16) NOT NULL DEFAULT 'file',

    -- 图片尺寸；普通文件允许为空。
    image_width INT,
    image_height INT,

    -- 标准审计字段。
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT fk_attachments_uploader
        FOREIGN KEY (uploader_user_id) REFERENCES users(user_id)
            ON DELETE RESTRICT,

    -- 基础非空白约束。
    CONSTRAINT ck_attachments_id_not_blank
        CHECK (btrim(attachment_id) <> ''),
    CONSTRAINT ck_attachments_uploader_not_blank
        CHECK (btrim(uploader_user_id) <> ''),
    CONSTRAINT ck_attachments_storage_provider_not_blank
        CHECK (btrim(storage_provider) <> ''),
    CONSTRAINT ck_attachments_storage_key_not_blank
        CHECK (btrim(storage_key) <> ''),
    CONSTRAINT ck_attachments_original_file_name_not_blank
        CHECK (btrim(original_file_name) <> ''),
    CONSTRAINT ck_attachments_mime_type_not_blank
        CHECK (btrim(mime_type) <> ''),
    CONSTRAINT ck_attachments_sha256_not_blank
        CHECK (sha256 IS NULL OR btrim(sha256) <> ''),

    -- 大小和尺寸不能为负。
    CONSTRAINT ck_attachments_size_non_negative
        CHECK (size_bytes >= 0),
    CONSTRAINT ck_attachments_image_size_valid
        CHECK (
            (image_width IS NULL AND image_height IS NULL) OR
            (image_width > 0 AND image_height > 0)
        ),

    -- 当前媒体类别先收口到图片 / 普通文件。
    CONSTRAINT ck_attachments_media_kind
        CHECK (media_kind IN ('image', 'file')),

    -- 当前存储提供者白名单。
    CONSTRAINT ck_attachments_storage_provider
        CHECK (storage_provider IN ('local', 'minio'))
);

-- storage_key 应稳定对应一个已落盘对象。
CREATE UNIQUE INDEX ux_attachments_storage_key
    ON attachments (storage_key);

-- 上传者自己的附件列表通常按时间倒序查看。
CREATE INDEX ix_attachments_uploader_created_at
    ON attachments (uploader_user_id, created_at DESC);

-- 若后续做哈希去重或校验，可直接按 sha256 检索。
CREATE INDEX ix_attachments_sha256
    ON attachments (sha256)
    WHERE sha256 IS NOT NULL;

-- attachments.updated_at 自动维护。
CREATE TRIGGER trg_attachments_set_updated_at
    BEFORE UPDATE ON attachments
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

COMMENT ON TABLE attachments IS
    '附件主表，记录上传成功后的文件元数据和 storage_key。';
COMMENT ON COLUMN attachments.attachment_id IS
    '对外稳定暴露的附件标识，例如 att_9001。';
COMMENT ON COLUMN attachments.uploader_user_id IS
    '上传者用户 ID，关联 users.user_id。';
COMMENT ON COLUMN attachments.storage_provider IS
    '底层存储提供者，例如 local 或 minio。';
COMMENT ON COLUMN attachments.storage_key IS
    '底层存储返回的稳定定位 key，例如 files/2026/03/20/file_xxx.png。';
COMMENT ON COLUMN attachments.original_file_name IS
    '用户上传时的原始文件名。';
COMMENT ON COLUMN attachments.mime_type IS
    '附件 MIME 类型，例如 image/png。';
COMMENT ON COLUMN attachments.size_bytes IS
    '附件字节大小。';
COMMENT ON COLUMN attachments.sha256 IS
    '附件内容摘要，第一版允许为空。';
COMMENT ON COLUMN attachments.media_kind IS
    '附件媒体类别：image / file。';
COMMENT ON COLUMN attachments.image_width IS
    '图片附件宽度，普通文件为空。';
COMMENT ON COLUMN attachments.image_height IS
    '图片附件高度，普通文件为空。';
