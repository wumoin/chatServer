#include "repository/attachment_repository.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <exception>
#include <optional>
#include <string>

namespace chatserver::repository {
namespace {

constexpr auto kInsertAttachmentSql = R"SQL(
INSERT INTO attachments (
    attachment_id,
    uploader_user_id,
    storage_provider,
    storage_key,
    original_file_name,
    mime_type,
    size_bytes,
    sha256,
    media_kind,
    image_width,
    image_height
)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
RETURNING
    attachment_id,
    uploader_user_id,
    storage_provider,
    storage_key,
    original_file_name,
    mime_type,
    size_bytes,
    sha256,
    media_kind,
    image_width,
    image_height,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
)SQL";

constexpr auto kFindAttachmentByIdSql = R"SQL(
SELECT
    attachment_id,
    uploader_user_id,
    storage_provider,
    storage_key,
    original_file_name,
    mime_type,
    size_bytes,
    sha256,
    media_kind,
    image_width,
    image_height,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
FROM attachments
WHERE attachment_id = $1
LIMIT 1
)SQL";

constexpr auto kDeleteAttachmentByIdSql = R"SQL(
DELETE FROM attachments
WHERE attachment_id = $1
)SQL";

AttachmentRecord toAttachmentRecord(const drogon::orm::Row &row)
{
    // repository 层负责把底层 Row 转成稳定的业务记录结构，
    // service 层不需要关心列名和可空列的细节。
    AttachmentRecord record;
    record.attachmentId = row["attachment_id"].as<std::string>();
    record.uploaderUserId = row["uploader_user_id"].as<std::string>();
    record.storageProvider = row["storage_provider"].as<std::string>();
    record.storageKey = row["storage_key"].as<std::string>();
    record.originalFileName = row["original_file_name"].as<std::string>();
    record.mimeType = row["mime_type"].as<std::string>();
    record.sizeBytes = row["size_bytes"].as<std::int64_t>();
    if (!row["sha256"].isNull())
    {
        record.sha256 = row["sha256"].as<std::string>();
    }
    record.mediaKind = row["media_kind"].as<std::string>();
    if (!row["image_width"].isNull())
    {
        record.imageWidth = row["image_width"].as<int>();
    }
    if (!row["image_height"].isNull())
    {
        record.imageHeight = row["image_height"].as<int>();
    }
    record.createdAtMs = row["created_at_ms"].as<std::int64_t>();
    return record;
}

}  // namespace

void AttachmentRepository::createAttachment(
    CreateAttachmentParams params,
    CreateAttachmentSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        // 这里只负责把附件元数据写入 attachments 表；
        // 文件落盘、权限校验、媒体类型判断都属于 service 层职责。
        client->execSqlAsync(
            kInsertAttachmentSql,
            [onSuccess = std::move(onSuccess),
             onFailure = std::move(onFailure)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onFailure("insert attachments returned no rows");
                    return;
                }

                onSuccess(toAttachmentRecord(rows[0]));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            params.attachmentId,
            params.uploaderUserId,
            params.storageProvider,
            params.storageKey,
            params.originalFileName,
            params.mimeType,
            params.sizeBytes,
            params.sha256,
            params.mediaKind,
            params.imageWidth,
            params.imageHeight);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void AttachmentRepository::findAttachmentById(
    std::string attachmentId,
    FindAttachmentByIdSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        // 下载接口先靠这条最小查询拿到附件元数据，再交给 service 去解析真实文件路径。
        client->execSqlAsync(
            kFindAttachmentByIdSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(std::nullopt);
                    return;
                }

                onSuccess(toAttachmentRecord(rows[0]));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(attachmentId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void AttachmentRepository::deleteAttachmentById(
    std::string attachmentId,
    DeleteAttachmentSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        // 这里的删除主要服务于“已准备正式附件，但后续消息落库失败”的补偿路径。
        client->execSqlAsync(
            kDeleteAttachmentByIdSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                onSuccess(rows.affectedRows() > 0);
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(attachmentId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

drogon::orm::DbClientPtr AttachmentRepository::dbClient() const
{
    return drogon::app().getDbClient("default");
}

}  // namespace chatserver::repository
