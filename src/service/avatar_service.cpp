#include "service/avatar_service.h"

#include "infra/log/app_logger.h"
#include "storage/file_storage.h"

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace chatserver::service {
namespace {

constexpr auto kAvatarLogTag = "avatar";
constexpr std::size_t kAvatarMaxBytes = 5 * 1024 * 1024;

bool startsWith(const std::string &text, const std::string_view prefix)
{
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin());
}

bool isImageMimeType(const std::string &contentType)
{
    return startsWith(contentType, "image/");
}

}  // namespace

bool AvatarService::uploadTemporaryAvatar(
    const TemporaryAvatarUploadRequest &request,
    TemporaryAvatarUploadView &out,
    ServiceError &error) const
{
    if (request.content.empty())
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "avatar file must not be empty";
        return false;
    }

    if (request.content.size() > kAvatarMaxBytes)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "avatar file size must not exceed 5 MB";
        return false;
    }

    if (!isImageMimeType(request.contentType))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "avatar file must be an image";
        return false;
    }

    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();

        storage::SaveFileRequest saveRequest;
        saveRequest.category = storage::FileCategory::kTemporary;
        saveRequest.originalFileName = request.originalFileName;
        saveRequest.contentType = request.contentType;

        const auto stored = storage->save(saveRequest, request.content);
        out.avatarUploadKey = stored.storageKey;
        out.previewUrl = "/api/v1/users/avatar/temp?avatar_upload_key=" +
                         drogon::utils::urlEncodeComponent(stored.storageKey);
        return true;
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_ERROR(kAvatarLogTag)
            << "failed to save temporary avatar: " << exception.what();
        error.code = protocol::error::ErrorCode::kInternalError;
        error.message = "failed to save temporary avatar";
        return false;
    }
}

bool AvatarService::confirmAvatarUploadKey(const std::string &avatarUploadKey,
                                           std::string &avatarStorageKey,
                                           ServiceError &error) const
{
    if (!isTemporaryAvatarUploadKey(avatarUploadKey))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "avatar_upload_key is invalid";
        return false;
    }

    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();
        if (!storage->exists(avatarUploadKey))
        {
            error.code = protocol::error::ErrorCode::kNotFound;
            error.message = "avatar upload key not found";
            return false;
        }

        const std::string content = storage->read(avatarUploadKey);

        storage::SaveFileRequest saveRequest;
        saveRequest.category = storage::FileCategory::kAvatar;
        saveRequest.originalFileName =
            std::filesystem::path(avatarUploadKey).filename().string();

        const auto stored = storage->save(saveRequest, content);
        avatarStorageKey = stored.storageKey;
        return true;
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_ERROR(kAvatarLogTag)
            << "failed to confirm avatar upload key: " << exception.what();
        error.code = protocol::error::ErrorCode::kInternalError;
        error.message = "failed to confirm avatar upload key";
        return false;
    }
}

bool AvatarService::resolveTemporaryAvatarPath(const std::string &avatarUploadKey,
                                               std::string &absolutePath,
                                               ServiceError &error) const
{
    if (!isTemporaryAvatarUploadKey(avatarUploadKey))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "avatar_upload_key is invalid";
        return false;
    }

    return resolveStoredAvatarPath(avatarUploadKey, absolutePath, error);
}

bool AvatarService::resolveStoredAvatarPath(const std::string &avatarStorageKey,
                                            std::string &absolutePath,
                                            ServiceError &error) const
{
    try
    {
        auto storage = storage::StorageRegistry::defaultStorage();
        if (!storage->exists(avatarStorageKey))
        {
            error.code = protocol::error::ErrorCode::kNotFound;
            error.message = "avatar file not found";
            return false;
        }

        absolutePath = storage->resolveAbsolutePath(avatarStorageKey).string();
        return true;
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_ERROR(kAvatarLogTag)
            << "failed to resolve avatar path: " << exception.what();
        error.code = protocol::error::ErrorCode::kInternalError;
        error.message = "failed to resolve avatar path";
        return false;
    }
}

void AvatarService::removeStorageKeyQuietly(const std::string &storageKey) const
{
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
        CHATSERVER_LOG_WARN(kAvatarLogTag)
            << "failed to cleanup storage key " << storageKey << ": "
            << exception.what();
    }
}

void AvatarService::removeStorageKeyQuietly(
    const std::optional<std::string> &storageKey) const
{
    if (!storageKey.has_value())
    {
        return;
    }

    removeStorageKeyQuietly(*storageKey);
}

bool AvatarService::isTemporaryAvatarUploadKey(
    const std::string &avatarUploadKey) const
{
    return startsWith(avatarUploadKey, "tmp/");
}

}  // namespace chatserver::service
