#include "service/file_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"
#include "storage/file_storage.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>

namespace chatserver::service {
namespace {

constexpr auto kFileLogTag = "file";
constexpr std::size_t kAttachmentMaxBytes = 50 * 1024 * 1024;

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

bool resolveCurrentUserClaims(const std::string &accessToken,
                              infra::security::AccessTokenClaims &claims)
{
    infra::security::TokenProvider tokenProvider;
    return tokenProvider.verifyAccessToken(accessToken, &claims);
}

}  // namespace

void FileService::uploadAttachment(UploadAttachmentRequest request,
                                   std::string accessToken,
                                   UploadAttachmentSuccess &&onSuccess,
                                   Failure &&onFailure) const
{
    // 上传流程分成两步：先把文件内容落到 storage，再写 attachments 元数据。
    // 只要第二步失败，就必须回滚第一步留下的文件，避免存储层出现孤儿文件。
    auto sharedSuccess =
        std::make_shared<UploadAttachmentSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    ServiceError validationError;
    if (!validateUploadAttachmentRequest(request, validationError))
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
    std::string storageProvider = "local";
    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();
        storageProvider = storage->providerName();
        storage::SaveFileRequest saveRequest;
        saveRequest.category = storage::FileCategory::kAttachment;
        saveRequest.originalFileName = request.originalFileName;
        saveRequest.contentType = request.mimeType;
        stored = storage->save(saveRequest, request.content);
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_ERROR(kFileLogTag)
            << "保存附件文件失败：" << exception.what();
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInternalError,
            "failed to save attachment file",
        });
        return;
    }

    infra::id::IdGenerator idGenerator;
    repository::CreateAttachmentParams params;
    params.attachmentId = idGenerator.nextAttachmentId();
    params.uploaderUserId = claims.userId;
    params.storageProvider = storageProvider;
    params.storageKey = stored.storageKey;
    params.originalFileName = stored.originalFileName;
    params.mimeType = stored.contentType.empty() ? request.mimeType : stored.contentType;
    params.sizeBytes = static_cast<std::int64_t>(stored.size);
    params.mediaKind = request.mediaKind;
    params.imageWidth = request.imageWidth;
    params.imageHeight = request.imageHeight;

    attachmentRepository_.createAttachment(
        std::move(params),
        [sharedSuccess,
         storageKey = stored.storageKey,
         this](repository::AttachmentRecord record) mutable {
            CHATSERVER_LOG_INFO(kFileLogTag)
                << "附件上传成功，attachment_id=" << record.attachmentId
                << " storage_key=" << storageKey;
            (*sharedSuccess)(toAttachmentView(record));
        },
        [sharedFailure, storageKey = stored.storageKey, this](
            std::string message) mutable {
            removeStorageKeyQuietly(storageKey);
            CHATSERVER_LOG_ERROR(kFileLogTag)
                << "写入附件元数据失败：" << message;
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

bool FileService::validateUploadAttachmentRequest(
    UploadAttachmentRequest &request,
    ServiceError &error) const
{
    // 第一版上传规则先保持简单明确：
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

    if (request.imageWidth.has_value() != request.imageHeight.has_value())
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "image width and height must be both present or both absent";
        return false;
    }

    return true;
}

protocol::dto::file::AttachmentView FileService::toAttachmentView(
    const repository::AttachmentRecord &record) const
{
    // 下载地址按当前服务端路由动态生成，而不是存数据库里的绝对 URL，
    // 这样以后改下载网关或域名时不需要回写历史数据。
    protocol::dto::file::AttachmentView view;
    view.attachmentId = record.attachmentId;
    view.fileName = record.originalFileName;
    view.mimeType = record.mimeType;
    view.sizeBytes = record.sizeBytes;
    view.mediaKind = record.mediaKind;
    view.downloadUrl = "/api/v1/files/" + record.attachmentId;
    view.storageKey = record.storageKey;
    view.imageWidth = record.imageWidth;
    view.imageHeight = record.imageHeight;
    view.createdAtMs = record.createdAtMs;
    return view;
}

void FileService::removeStorageKeyQuietly(const std::string &storageKey) const
{
    // 这是上传失败时的补偿删除。主流程已经失败了，所以这里即使再次失败也只记日志。
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
