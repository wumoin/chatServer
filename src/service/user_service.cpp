#include "service/user_service.h"

#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>

// UserService 负责“当前登录用户相关”的资料动作：
// - 临时头像上传
// - 资料更新
// - 用户搜索
// - 按 user_id 读取头像
//
// 认证是谁由 token provider 决定，这里负责的是用户域规则和响应视图拼装。
namespace chatserver::service {
namespace {

constexpr auto kUserProfileLogTag = "user.profile";
constexpr auto kAvatarUploadLogTag = "user.avatar";
constexpr auto kUserSearchLogTag = "user.search";

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

bool isAccountCharacterAllowed(const char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_' || ch == '.' || ch == '-';
}

protocol::dto::user::UserProfileView toUserProfileView(
    const repository::UserProfileRecord &record)
{
    protocol::dto::user::UserProfileView user;
    user.userId = record.userId;
    user.account = record.account;
    user.nickname = record.nickname;
    user.avatarUrl = record.avatarUrl;
    return user;
}

}  // namespace

void UserService::uploadTemporaryAvatar(TemporaryAvatarUploadRequest request,
                                        UploadTemporaryAvatarSuccess &&onSuccess,
                                        Failure &&onFailure) const
{
    ServiceError error;
    TemporaryAvatarUploadView result;
    if (!avatarService_.uploadTemporaryAvatar(request, result, error))
    {
        onFailure(std::move(error));
        return;
    }

    CHATSERVER_LOG_INFO(kAvatarUploadLogTag)
        << "临时头像上传成功，avatar_upload_key=" << result.avatarUploadKey;
    onSuccess(std::move(result));
}

void UserService::searchUserByAccount(
    std::string account,
    std::string accessToken,
    SearchUserByAccountSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<SearchUserByAccountSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    account = trimCopy(account);
    if (account.empty() || account.size() > 64)
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "account length must be between 1 and 64",
        });
        return;
    }

    if (!std::all_of(account.begin(), account.end(), isAccountCharacterAllowed))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "account may contain only letters, digits, '_', '.' and '-'",
        });
        return;
    }

    accessToken = trimCopy(accessToken);
    infra::security::TokenProvider tokenProvider;
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() ||
        !tokenProvider.verifyAccessToken(accessToken, &claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    userRepository_.findUserProfileByAccount(
        std::move(account),
        [sharedSuccess](std::optional<repository::UserProfileRecord> user) mutable {
            protocol::dto::user::UserSearchResultView result;
            result.exists = user.has_value();
            if (user.has_value())
            {
                protocol::dto::user::UserProfileView profile;
                profile.userId = std::move(user->userId);
                profile.account = std::move(user->account);
                profile.nickname = std::move(user->nickname);
                profile.avatarUrl = std::move(user->avatarUrl);
                result.user = std::move(profile);
            }

            (*sharedSuccess)(std::move(result));
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kUserSearchLogTag)
                << "按账号搜索用户失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to search user",
            });
        });
}

void UserService::updateProfile(
    protocol::dto::user::UpdateUserProfileRequest request,
    std::string accessToken,
    UpdateUserProfileSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<UpdateUserProfileSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    ServiceError validationError;
    if (!validateUpdateProfileRequest(request, validationError))
    {
        (*sharedFailure)(std::move(validationError));
        return;
    }

    accessToken = trimCopy(accessToken);
    infra::security::TokenProvider tokenProvider;
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() ||
        !tokenProvider.verifyAccessToken(accessToken, &claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    userRepository_.findUserById(
        claims.userId,
        [request = std::move(request),
         claims,
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::UserProfileRecord> currentUser) mutable {
            if (!currentUser.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "user not found",
                });
                return;
            }

            std::optional<std::string> confirmedAvatarStorageKey;
            if (request.hasAvatarUploadKey && request.avatarUploadKey.has_value())
            {
                std::string avatarStorageKey;
                ServiceError avatarError;
                if (!avatarService_.confirmAvatarUploadKey(
                        *request.avatarUploadKey,
                        avatarStorageKey,
                        avatarError))
                {
                    (*sharedFailure)(std::move(avatarError));
                    return;
                }
                confirmedAvatarStorageKey = std::move(avatarStorageKey);
            }

            repository::UpdateUserProfileParams params;
            params.userId = claims.userId;
            params.updateNickname = request.hasNickname && request.nickname.has_value();
            params.nickname = request.nickname;
            params.updateAvatarUrl =
                request.hasAvatarUploadKey && confirmedAvatarStorageKey.has_value();
            params.avatarUrl = confirmedAvatarStorageKey;

            userRepository_.updateUserProfile(
                std::move(params),
                [currentUser = std::move(currentUser),
                 request,
                 confirmedAvatarStorageKey,
                 sharedSuccess,
                 this](repository::UserProfileRecord record) mutable {
                    avatarService_.removeStorageKeyQuietly(request.avatarUploadKey);
                    if (confirmedAvatarStorageKey.has_value() &&
                        currentUser->avatarUrl.has_value() &&
                        currentUser->avatarUrl != confirmedAvatarStorageKey)
                    {
                        avatarService_.removeStorageKeyQuietly(currentUser->avatarUrl);
                    }

                    CHATSERVER_LOG_INFO(kUserProfileLogTag)
                        << "用户资料更新成功，user_id=" << record.userId;
                    (*sharedSuccess)(toUserProfileView(record));
                },
                [confirmedAvatarStorageKey,
                 sharedFailure,
                 this](std::string message) mutable {
                    avatarService_.removeStorageKeyQuietly(confirmedAvatarStorageKey);
                    CHATSERVER_LOG_ERROR(kUserProfileLogTag)
                        << "更新用户资料失败：" << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to update user profile",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kUserProfileLogTag)
                << "查询当前用户信息失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query user",
            });
        });
}

void UserService::getUserAvatarFile(std::string userId,
                                    ResolveFileSuccess &&onSuccess,
                                    Failure &&onFailure) const
{
    userRepository_.findUserById(
        std::move(userId),
        [onSuccess = std::move(onSuccess),
         onFailure = std::move(onFailure),
         this](std::optional<repository::UserProfileRecord> user) mutable {
            if (!user.has_value())
            {
                onFailure(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "user not found",
                });
                return;
            }

            if (!user->avatarUrl.has_value())
            {
                onFailure(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "avatar not found",
                });
                return;
            }

            ServiceError error;
            std::string absolutePath;
            if (!avatarService_.resolveStoredAvatarPath(*user->avatarUrl,
                                                       absolutePath,
                                                       error))
            {
                onFailure(std::move(error));
                return;
            }

            onSuccess(FileResolveResult{absolutePath});
        },
        [onFailure = std::move(onFailure)](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kUserProfileLogTag)
                << "查询用户头像失败：" << message;
            onFailure(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query user",
            });
        });
}

void UserService::getTemporaryAvatarFile(std::string avatarUploadKey,
                                         ResolveFileSuccess &&onSuccess,
                                         Failure &&onFailure) const
{
    ServiceError error;
    std::string absolutePath;
    if (!avatarService_.resolveTemporaryAvatarPath(avatarUploadKey,
                                                   absolutePath,
                                                   error))
    {
        onFailure(std::move(error));
        return;
    }

    onSuccess(FileResolveResult{absolutePath});
}

bool UserService::validateUpdateProfileRequest(
    protocol::dto::user::UpdateUserProfileRequest &request,
    ServiceError &error) const
{
    if (!request.hasNickname && !request.hasAvatarUploadKey)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "at least one profile field must be provided";
        return false;
    }

    if (request.hasNickname)
    {
        if (!request.nickname.has_value())
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "nickname must not be null";
            return false;
        }

        request.nickname = trimCopy(*request.nickname);
        if (request.nickname->empty() || request.nickname->size() > 64)
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "nickname length must be between 1 and 64";
            return false;
        }
    }

    if (request.hasAvatarUploadKey)
    {
        if (!request.avatarUploadKey.has_value())
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "avatar_upload_key must not be null";
            return false;
        }

        request.avatarUploadKey = trimCopy(*request.avatarUploadKey);
        if (request.avatarUploadKey->empty() ||
            request.avatarUploadKey->size() > 2048)
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "avatar_upload_key is invalid";
            return false;
        }
    }

    return true;
}

}  // namespace chatserver::service
