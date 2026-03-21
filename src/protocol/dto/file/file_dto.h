#pragma once

#include <json/value.h>

#include <cstdint>
#include <optional>
#include <string>

namespace chatserver::protocol::dto::file {

/**
 * @brief 临时附件上传成功后返回给客户端的最小视图。
 *
 * 这里返回的是“临时上传引用”，还不是已经正式入库的 attachment。
 * 客户端后续需要把 `attachment_upload_key` 带到 `message.send_image`
 * / `message.send_file`
 * 等正式业务动作里，服务端才会把临时文件转成正式附件并写数据库。
 */
struct TemporaryAttachmentUploadView
{
    std::string attachmentUploadKey;
    std::string fileName;
    std::string mimeType;
    std::int64_t sizeBytes{0};
    std::string mediaKind;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

inline Json::Value toJson(const TemporaryAttachmentUploadView &upload)
{
    Json::Value value(Json::objectValue);
    value["attachment_upload_key"] = upload.attachmentUploadKey;
    value["file_name"] = upload.fileName;
    value["mime_type"] = upload.mimeType;
    value["size_bytes"] = Json::Int64(upload.sizeBytes);
    value["media_kind"] = upload.mediaKind;

    if (upload.imageWidth.has_value())
    {
        value["image_width"] = *upload.imageWidth;
    }
    if (upload.imageHeight.has_value())
    {
        value["image_height"] = *upload.imageHeight;
    }

    return value;
}

/**
 * @brief 正式附件上传确认后返回给客户端的最小视图。
 *
 * 这里描述的是已经进入 `attachments` 表、并可供消息或下载接口引用的正式附件。
 */
struct AttachmentView
{
    std::string attachmentId;
    std::string fileName;
    std::string mimeType;
    std::int64_t sizeBytes{0};
    std::string mediaKind;
    std::string downloadUrl;
    std::optional<std::string> storageKey;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
    std::optional<std::int64_t> createdAtMs;
};

inline Json::Value toJson(const AttachmentView &attachment)
{
    Json::Value value(Json::objectValue);
    value["attachment_id"] = attachment.attachmentId;
    value["file_name"] = attachment.fileName;
    value["mime_type"] = attachment.mimeType;
    value["size_bytes"] = Json::Int64(attachment.sizeBytes);
    value["media_kind"] = attachment.mediaKind;
    value["download_url"] = attachment.downloadUrl;

    if (attachment.storageKey.has_value())
    {
        value["storage_key"] = *attachment.storageKey;
    }
    if (attachment.imageWidth.has_value())
    {
        value["image_width"] = *attachment.imageWidth;
    }
    if (attachment.imageHeight.has_value())
    {
        value["image_height"] = *attachment.imageHeight;
    }
    if (attachment.createdAtMs.has_value())
    {
        value["created_at_ms"] = Json::Int64(*attachment.createdAtMs);
    }

    return value;
}

}  // namespace chatserver::protocol::dto::file
