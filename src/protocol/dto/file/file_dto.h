#pragma once

#include <json/value.h>

#include <cstdint>
#include <optional>
#include <string>

namespace chatserver::protocol::dto::file {

/**
 * @brief 附件上传成功后返回给客户端的最小视图。
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
