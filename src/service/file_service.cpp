#include "service/file_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"
#include "storage/file_storage.h"

#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace chatserver::service {
namespace {

constexpr auto kFileLogTag = "file";
constexpr std::size_t kAttachmentMaxBytes = 50 * 1024 * 1024;
constexpr std::string_view kTemporaryAttachmentPrefix = "tmp/attachments/";

/**
 * @brief 临时附件旁路元数据。
 *
 * 这份元数据不会直接暴露给外部数据库表。
 * 它的作用是：
 * 1) 在正式入库前保留“原始文件名 / MIME / 上传人”等可信信息；
 * 2) 让 `message.send_image` 只需引用 upload key，就能完成正式确认；
 * 3) 避免把临时附件也提前写进 `attachments` 表。
 */
struct TemporaryAttachmentMetadata
{
    std::string uploaderUserId;
    std::string originalFileName;
    std::string mimeType;
    std::string mediaKind;
    std::int64_t sizeBytes{0};
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

std::string trimCopy(const std::string_view input)
{
    std::size_t begin = 0;
    std::size_t end = input.size();

    while (begin < end &&
           std::isspace(static_cast<unsigned char>(input[begin])) != 0)
    {
        ++begin;
    }

    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
    {
        --end;
    }

    return std::string(input.substr(begin, end - begin));
}

bool startsWith(const std::string &text, const std::string_view prefix)
{
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin());
}

std::string normalizeExtension(const std::string &fileName)
{
    const auto extension = std::filesystem::path(fileName).extension().string();
    if (extension.empty() || extension.size() > 16)
    {
        return {};
    }

    std::string normalized;
    normalized.reserve(extension.size());
    for (const unsigned char ch : extension)
    {
        if (std::isalnum(ch) || ch == '.')
        {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }

    if (normalized.empty() || normalized == ".")
    {
        return {};
    }

    return normalized;
}

std::string normalizePathSegment(const std::string &text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text)
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-')
        {
            normalized.push_back(static_cast<char>(ch));
        }
    }

    return normalized;
}

std::string currentDatePath()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y/%m/%d");
    return stream.str();
}

std::string buildTemporaryAttachmentUploadKey(const std::string &userId,
                                              const std::string &fileName)
{
    const std::string safeUserId = normalizePathSegment(userId);
    if (safeUserId.empty())
    {
        throw std::runtime_error(
            "failed to build temporary attachment key: invalid user id");
    }

    return (std::filesystem::path("tmp") / "attachments" / safeUserId /
            currentDatePath() /
            ("tmp_upload_" + drogon::utils::getUuid(false).substr(0, 24) +
             normalizeExtension(fileName)))
        .generic_string();
}

std::string metadataStorageKeyForUploadKey(const std::string &attachmentUploadKey)
{
    if (attachmentUploadKey.empty())
    {
        return {};
    }

    return attachmentUploadKey + ".meta.json";
}

Json::Value toJson(const TemporaryAttachmentMetadata &metadata)
{
    Json::Value json(Json::objectValue);
    json["uploader_user_id"] = metadata.uploaderUserId;
    json["original_file_name"] = metadata.originalFileName;
    json["mime_type"] = metadata.mimeType;
    json["media_kind"] = metadata.mediaKind;
    json["size_bytes"] = Json::Int64(metadata.sizeBytes);
    if (metadata.imageWidth.has_value())
    {
        json["image_width"] = *metadata.imageWidth;
    }
    if (metadata.imageHeight.has_value())
    {
        json["image_height"] = *metadata.imageHeight;
    }
    return json;
}

std::string writeJsonCompact(const Json::Value &json)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    return Json::writeString(builder, json);
}

bool parseTemporaryAttachmentMetadata(const std::string &text,
                                      TemporaryAttachmentMetadata &out)
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    Json::Value root;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(),
                       text.data() + text.size(),
                       &root,
                       &errors) ||
        !root.isObject())
    {
        return false;
    }

    if (!root.isMember("uploader_user_id") ||
        !root["uploader_user_id"].isString() ||
        !root.isMember("original_file_name") ||
        !root["original_file_name"].isString() ||
        !root.isMember("mime_type") || !root["mime_type"].isString() ||
        !root.isMember("media_kind") || !root["media_kind"].isString() ||
        !root.isMember("size_bytes") || !root["size_bytes"].isInt64())
    {
        return false;
    }

    TemporaryAttachmentMetadata parsed;
    parsed.uploaderUserId = root["uploader_user_id"].asString();
    parsed.originalFileName = root["original_file_name"].asString();
    parsed.mimeType = root["mime_type"].asString();
    parsed.mediaKind = root["media_kind"].asString();
    parsed.sizeBytes = root["size_bytes"].asInt64();
    if (root.isMember("image_width") && root["image_width"].isInt())
    {
        parsed.imageWidth = root["image_width"].asInt();
    }
    if (root.isMember("image_height") && root["image_height"].isInt())
    {
        parsed.imageHeight = root["image_height"].asInt();
    }

    out = std::move(parsed);
    return true;
}

bool resolveCurrentUserClaims(const std::string &accessToken,
                              infra::security::AccessTokenClaims &claims)
{
    infra::security::TokenProvider tokenProvider;
    return tokenProvider.verifyAccessToken(accessToken, &claims);
}

}  // namespace

void FileService::uploadTemporaryAttachment(
    TemporaryAttachmentUploadRequest request,
    std::string accessToken,
    UploadTemporaryAttachmentSuccess &&onSuccess,
    Failure &&onFailure) const
{
    // 附件 HTTP 上传阶段只负责两件事：
    // 1. 把原始文件放进 tmp 目录；
    // 2. 把上传所需的可信元数据写到旁路 meta 文件。
    //
    // 这一步不写 attachments 表。
    // 只有后续 message.send_image 等正式业务动作确认成功时，
    // 才会把临时附件转成正式附件并写库。
    auto sharedSuccess =
        std::make_shared<UploadTemporaryAttachmentSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    ServiceError validationError;
    if (!validateTemporaryAttachmentUploadRequest(request, validationError))
    {
        (*sharedFailure)(std::move(validationError));
        return;
    }

    accessToken = trimCopy(accessToken);
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    storage::StoredFileInfo stored;
    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();

        storage::SaveFileRequest saveRequest;
        saveRequest.category = storage::FileCategory::kTemporary;
        saveRequest.originalFileName = request.originalFileName;
        saveRequest.contentType = request.mimeType;
        // 临时附件把 user_id 编进 upload key，便于后续 message.send_image
        // 直接校验“这次上传是不是当前用户自己的”。
        saveRequest.preferredStorageKey =
            buildTemporaryAttachmentUploadKey(claims.userId,
                                             request.originalFileName);
        stored = storage->save(saveRequest, request.content);

        TemporaryAttachmentMetadata metadata;
        metadata.uploaderUserId = claims.userId;
        metadata.originalFileName = request.originalFileName;
        metadata.mimeType =
            stored.contentType.empty() ? request.mimeType : stored.contentType;
        metadata.mediaKind = request.mediaKind;
        metadata.sizeBytes = static_cast<std::int64_t>(stored.size);
        metadata.imageWidth = request.imageWidth;
        metadata.imageHeight = request.imageHeight;

        storage::SaveFileRequest metadataRequest;
        metadataRequest.category = storage::FileCategory::kTemporary;
        metadataRequest.originalFileName =
            std::filesystem::path(stored.storageKey).filename().string() +
            ".meta.json";
        metadataRequest.contentType = "application/json";
        metadataRequest.preferredStorageKey =
            metadataStorageKeyForUploadKey(stored.storageKey);
        storage->save(metadataRequest, writeJsonCompact(toJson(metadata)));

        TemporaryAttachmentUploadView view;
        view.attachmentUploadKey = stored.storageKey;
        view.fileName = metadata.originalFileName;
        view.mimeType = metadata.mimeType;
        view.sizeBytes = metadata.sizeBytes;
        view.mediaKind = metadata.mediaKind;
        view.imageWidth = metadata.imageWidth;
        view.imageHeight = metadata.imageHeight;

        CHATSERVER_LOG_INFO(kFileLogTag)
            << "临时附件上传成功，user_id=" << claims.userId
            << " upload_key=" << view.attachmentUploadKey
            << " media_kind=" << view.mediaKind;
        (*sharedSuccess)(std::move(view));
    }
    catch (const std::exception &exception)
    {
        // temp 文件写成功但 meta 写失败时，要把已经落下的临时对象一并清理掉，
        // 避免之后出现“只有文件，没有元数据”的不可确认上传。
        removeTemporaryUploadQuietly(stored.storageKey);
        CHATSERVER_LOG_ERROR(kFileLogTag)
            << "保存临时附件失败：" << exception.what();
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInternalError,
            "failed to save temporary attachment",
        });
    }
}

void FileService::prepareAttachmentForMessage(
    std::string attachmentUploadKey,
    std::string uploaderUserId,
    std::optional<std::string> expectedMediaKind,
    PrepareAttachmentSuccess &&onSuccess,
    Failure &&onFailure) const
{
    // 这一步承担“临时上传 -> 正式附件”的转换：
    // 1. 校验 upload key 是否存在且归属于当前用户；
    // 2. 读取临时文件和可信 meta；
    // 3. 把文件写入正式 attachments 目录；
    // 4. 创建 attachments 表记录。
    //
    // 注意：
    // - 这里成功后，临时文件不会立刻删除；
    // - 调用方只有在后续消息落库成功后，才应该清理临时上传；
    // - 如果消息落库失败，应回滚这里创建出的正式附件，但保留临时上传以便重试。
    auto sharedSuccess =
        std::make_shared<PrepareAttachmentSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    attachmentUploadKey = trimCopy(attachmentUploadKey);
    uploaderUserId = trimCopy(uploaderUserId);
    if (attachmentUploadKey.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "attachment_upload_key is required",
        });
        return;
    }

    if (uploaderUserId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "uploader_user_id is required",
        });
        return;
    }

    if (!startsWith(attachmentUploadKey, kTemporaryAttachmentPrefix))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "attachment_upload_key is invalid",
        });
        return;
    }

    TemporaryAttachmentMetadata metadata;
    std::string content;
    storage::StoredFileInfo stored;
    std::string storageProvider = "local";
    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();
        storageProvider = storage->providerName();
        if (!storage->exists(attachmentUploadKey))
        {
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kNotFound,
                "attachment upload key not found",
            });
            return;
        }

        const std::string metadataStorageKey =
            metadataStorageKeyForUploadKey(attachmentUploadKey);
        if (!storage->exists(metadataStorageKey))
        {
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kNotFound,
                "attachment upload metadata not found",
            });
            return;
        }

        const std::string metadataText = storage->read(metadataStorageKey);
        if (!parseTemporaryAttachmentMetadata(metadataText, metadata))
        {
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "attachment upload metadata is invalid",
            });
            return;
        }

        // upload key 既带路径前缀约束，也再用 meta 中的 uploader_user_id 做一次强校验，
        // 避免调用方拿到别人的 upload key 后冒充确认。
        if (metadata.uploaderUserId != uploaderUserId)
        {
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kNotFound,
                "attachment upload key not found",
            });
            return;
        }

        if (expectedMediaKind.has_value())
        {
            *expectedMediaKind = trimCopy(*expectedMediaKind);
            if (!expectedMediaKind->empty() &&
                metadata.mediaKind != *expectedMediaKind)
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInvalidArgument,
                    "attachment upload key media kind does not match message type",
                });
                return;
            }
        }

        content = storage->read(attachmentUploadKey);
        if (content.empty())
        {
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInvalidArgument,
                "attachment file must not be empty",
            });
            return;
        }

        if (static_cast<std::int64_t>(content.size()) != metadata.sizeBytes)
        {
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "attachment upload metadata does not match file content",
            });
            return;
        }

        storage::SaveFileRequest saveRequest;
        saveRequest.category = storage::FileCategory::kAttachment;
        saveRequest.originalFileName = metadata.originalFileName;
        saveRequest.contentType = metadata.mimeType;
        stored = storage->save(saveRequest, content);
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_ERROR(kFileLogTag)
            << "准备正式附件文件失败，upload_key=" << attachmentUploadKey
            << "，原因：" << exception.what();
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInternalError,
            "failed to prepare attachment file",
        });
        return;
    }

    infra::id::IdGenerator idGenerator;
    repository::CreateAttachmentParams params;
    params.attachmentId = idGenerator.nextAttachmentId();
    params.uploaderUserId = uploaderUserId;
    params.storageProvider = storageProvider;
    params.storageKey = stored.storageKey;
    params.originalFileName = metadata.originalFileName;
    params.mimeType = metadata.mimeType;
    params.sizeBytes = metadata.sizeBytes;
    params.mediaKind = metadata.mediaKind;
    params.imageWidth = metadata.imageWidth;
    params.imageHeight = metadata.imageHeight;

    attachmentRepository_.createAttachment(
        std::move(params),
        [sharedSuccess,
         attachmentUploadKey](repository::AttachmentRecord record) mutable {
            PreparedAttachmentResult result;
            result.attachmentUploadKey = attachmentUploadKey;
            result.attachment = std::move(record);
            (*sharedSuccess)(std::move(result));
        },
        [sharedFailure, storageKey = stored.storageKey, this](
            std::string message) mutable {
            removeStorageKeyQuietly(storageKey);
            CHATSERVER_LOG_ERROR(kFileLogTag)
                << "创建正式附件记录失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to create attachment record",
            });
        });
}

void FileService::getAttachmentFile(std::string attachmentId,
                                    std::string accessToken,
                                    DownloadAttachmentSuccess &&onSuccess,
                                    Failure &&onFailure) const
{
    // 当前下载策略还是第一版：只要求调用方已登录。
    // 后续如果要把附件权限绑定到消息 / 会话成员关系，可以在这里继续收紧。
    auto sharedSuccess =
        std::make_shared<DownloadAttachmentSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    attachmentId = trimCopy(attachmentId);
    if (attachmentId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "attachment_id must not be empty",
        });
        return;
    }

    accessToken = trimCopy(accessToken);
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    attachmentRepository_.findAttachmentById(
        std::move(attachmentId),
        [sharedSuccess, sharedFailure](
            std::optional<repository::AttachmentRecord> attachment) mutable {
            if (!attachment.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "attachment not found",
                });
                return;
            }

            try
            {
                auto storage = storage::StorageRegistry::defaultStorage();
                if (!storage->exists(attachment->storageKey))
                {
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kNotFound,
                        "attachment file not found",
                    });
                    return;
                }

                (*sharedSuccess)(DownloadAttachmentResult{
                    storage->resolveAbsolutePath(attachment->storageKey).string(),
                    attachment->mimeType,
                });
            }
            catch (const std::exception &exception)
            {
                CHATSERVER_LOG_ERROR(kFileLogTag)
                    << "解析附件路径失败：" << exception.what();
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInternalError,
                    "failed to resolve attachment file",
                });
            }
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFileLogTag)
                << "查询附件元数据失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query attachment",
            });
        });
}

void FileService::removeTemporaryUploadQuietly(
    const std::string &attachmentUploadKey) const
{
    // 临时上传由“文件体 + meta 文件”两部分组成。
    // 正式消息链路完成后，两者都应一起清理。
    if (attachmentUploadKey.empty())
    {
        return;
    }

    removeStorageKeyQuietly(attachmentUploadKey);
    removeStorageKeyQuietly(metadataStorageKeyForUploadKey(attachmentUploadKey));
}

void FileService::rollbackPreparedAttachmentQuietly(
    const repository::AttachmentRecord &attachment) const
{
    // 这里是“正式附件已准备完成，但后续消息入库失败”的补偿路径。
    // 目标是把刚刚创建的正式附件尽量删干净，同时保留临时上传让调用方可以重试。
    if (!attachment.attachmentId.empty())
    {
        attachmentRepository_.deleteAttachmentById(
            attachment.attachmentId,
            [attachmentId = attachment.attachmentId](const bool deleted) {
                CHATSERVER_LOG_INFO(kFileLogTag)
                    << "已回滚正式附件记录，attachment_id=" << attachmentId
                    << " deleted=" << deleted;
            },
            [attachmentId = attachment.attachmentId](std::string message) {
                CHATSERVER_LOG_WARN(kFileLogTag)
                    << "回滚正式附件记录失败，attachment_id=" << attachmentId
                    << "，原因：" << message;
            });
    }

    removeStorageKeyQuietly(attachment.storageKey);
}

bool FileService::validateTemporaryAttachmentUploadRequest(
    TemporaryAttachmentUploadRequest &request,
    ServiceError &error) const
{
    // 第一版临时附件上传规则先保持简单明确：
    // 1. 文件名必填
    // 2. 文件内容不能为空
    // 3. 限制最大体积
    // 4. media_kind 只允许 image / file
    // 5. 图片尺寸要么都带，要么都不带
    request.originalFileName = trimCopy(request.originalFileName);
    request.mimeType = trimCopy(request.mimeType);
    request.mediaKind = trimCopy(request.mediaKind);

    if (request.originalFileName.empty() || request.originalFileName.size() > 255)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "file name length must be between 1 and 255";
        return false;
    }

    if (request.content.empty())
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "file must not be empty";
        return false;
    }

    if (request.content.size() > kAttachmentMaxBytes)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "file size must not exceed 50 MB";
        return false;
    }

    if (request.mimeType.empty())
    {
        request.mimeType = "application/octet-stream";
    }

    if (request.mediaKind.empty())
    {
        request.mediaKind = "file";
    }

    if (request.mediaKind != "image" && request.mediaKind != "file")
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "media kind must be 'image' or 'file'";
        return false;
    }

    if (request.mediaKind == "image" && !startsWith(request.mimeType, "image/"))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "image attachment mime type must start with image/";
        return false;
    }

    if (request.imageWidth.has_value() != request.imageHeight.has_value())
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "image width and height must be both present or both absent";
        return false;
    }

    if (request.imageWidth.has_value() && *request.imageWidth <= 0)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "image width must be greater than 0";
        return false;
    }

    if (request.imageHeight.has_value() && *request.imageHeight <= 0)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "image height must be greater than 0";
        return false;
    }

    return true;
}

void FileService::removeStorageKeyQuietly(const std::string &storageKey) const
{
    // 这是上传失败或回滚失败时的补偿删除。主流程已经结束了，
    // 所以这里即使再次失败也只记日志，不再向外抛业务错误。
    if (storageKey.empty())
    {
        return;
    }

    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();
        storage->remove(storageKey);
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_WARN(kFileLogTag)
            << "清理附件文件失败，storage_key=" << storageKey << "，原因："
            << exception.what();
    }
}

}  // namespace chatserver::service
