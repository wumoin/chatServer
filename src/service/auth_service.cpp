#include "service/auth_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "infra/security/password_hasher.h"
#include "infra/security/token_provider.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chatserver::service {
namespace {

constexpr auto kAuthRegisterLogTag = "auth.register";
constexpr auto kAuthLoginLogTag = "auth.login";
constexpr auto kAuthLogoutLogTag = "auth.logout";

std::string trimCopy(const std::string_view input)
{
    // 统一做一个“复制版 trim”工具，而不是原地改字符串：
    // 1) 方便对 account / nickname / avatar_url 复用；
    // 2) 不会在规则判断前悄悄改变原值；
    // 3) 只有明确需要规范化时才把结果写回请求对象。
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
    // 第一版先收紧账号字符集，减少后续登录、URL、日志和排障时的复杂度。
    // 当前允许：
    // - 字母
    // - 数字
    // - `_`、`.`、`-`
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_' || ch == '.' || ch == '-';
}

bool isDevicePlatformCharacterAllowed(const char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_' || ch == '-';
}

}  // namespace

void AuthService::registerUser(protocol::dto::auth::RegisterRequest request,
                               RegisterSuccess &&onSuccess,
                               RegisterFailure &&onFailure) const
{
    ServiceError validationError;
    if (!validateRegisterRequest(request, validationError))
    {
        onFailure(std::move(validationError));
        return;
    }

    try
    {
        // 当前阶段 password hash 仍然在业务线程里同步完成。
        // 注册流量通常远低于消息流量，这样先把闭环打通；
        // 后续如果注册量上来，再把这一步下沉到专门任务线程。
        infra::id::IdGenerator idGenerator;
        infra::security::PasswordHasher passwordHasher;

        repository::CreateUserParams params;
        params.userId = idGenerator.nextUserId();
        params.account = request.account;
        params.nickname = request.nickname;
        params.avatarUrl = request.avatarUrl;
        params.passwordHash = passwordHasher.hashPassword(request.password);
        params.passwordAlgo = "bcrypt";

        // 注册成功后当前只写 users，不会自动创建 device_session。
        // 因此这一步结束后客户端只能拿到用户资料，不能直接视为“已登录”。
        userRepository_.createUser(
            std::move(params),
            [onSuccess = std::move(onSuccess)](
                repository::CreatedUserRecord record) mutable {
                protocol::dto::auth::RegisterUserView user;
                user.userId = std::move(record.userId);
                user.account = std::move(record.account);
                user.nickname = std::move(record.nickname);
                user.avatarUrl = std::move(record.avatarUrl);
                user.createdAtMs = record.createdAtMs;

                onSuccess(std::move(user));
            },
            [onFailure = std::move(onFailure)](
                const repository::CreateUserError &error) mutable {
                ServiceError serviceError;

                switch (error.kind)
                {
                case repository::CreateUserErrorKind::kAccountAlreadyExists:
                    serviceError.code =
                        protocol::error::ErrorCode::kAccountAlreadyExists;
                    serviceError.message = "account already exists";
                    CHATSERVER_LOG_WARN(kAuthRegisterLogTag)
                        << "register rejected because account already exists";
                    break;
                case repository::CreateUserErrorKind::kDatabaseError:
                default:
                    serviceError.code =
                        protocol::error::ErrorCode::kInternalError;
                    serviceError.message = "failed to create user";
                    CHATSERVER_LOG_ERROR(kAuthRegisterLogTag)
                        << "register failed while inserting user: "
                        << error.message;
                    break;
                }

                onFailure(std::move(serviceError));
            });
    }
    catch (const std::exception &exception)
    {
        CHATSERVER_LOG_ERROR(kAuthRegisterLogTag)
            << "register failed before database insert: " << exception.what();

        onFailure(ServiceError{
            protocol::error::ErrorCode::kInternalError,
            "failed to hash password",
        });
    }
}

void AuthService::loginUser(protocol::dto::auth::LoginRequest request,
                            LoginRequestContext context,
                            LoginSuccess &&onSuccess,
                            LoginFailure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<LoginSuccess>(std::move(onSuccess));
    auto sharedFailure =
        std::make_shared<LoginFailure>(std::move(onFailure));

    ServiceError validationError;
    if (!validateLoginRequest(request, validationError))
    {
        (*sharedFailure)(std::move(validationError));
        return;
    }

    const std::string accountForLookup = request.account;
    userRepository_.findUserByAccount(
        accountForLookup,
        [request = std::move(request),
         context = std::move(context),
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::AuthUserRecord> userRecord) mutable {
            if (!userRecord.has_value())
            {
                CHATSERVER_LOG_WARN(kAuthLoginLogTag)
                    << "login rejected because account does not exist";
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInvalidCredentials,
                    "invalid credentials",
                });
                return;
            }

            if (userRecord->accountStatus == "disabled")
            {
                CHATSERVER_LOG_WARN(kAuthLoginLogTag)
                    << "login rejected because account is disabled";
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kAccountDisabled,
                    "account disabled",
                });
                return;
            }

            if (userRecord->accountStatus == "locked")
            {
                CHATSERVER_LOG_WARN(kAuthLoginLogTag)
                    << "login rejected because account is locked";
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kAccountLocked,
                    "account locked",
                });
                return;
            }

            if (userRecord->passwordAlgo != "bcrypt")
            {
                CHATSERVER_LOG_ERROR(kAuthLoginLogTag)
                    << "login failed because password algorithm is unsupported: "
                    << userRecord->passwordAlgo;
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInternalError,
                    "unsupported password algorithm",
                });
                return;
            }

            infra::security::PasswordHasher passwordHasher;
            if (!passwordHasher.verifyPassword(request.password,
                                               userRecord->passwordHash))
            {
                CHATSERVER_LOG_WARN(kAuthLoginLogTag)
                    << "login rejected because password does not match";
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInvalidCredentials,
                    "invalid credentials",
                });
                return;
            }

            try
            {
                infra::id::IdGenerator idGenerator;
                infra::security::TokenProvider tokenProvider;

                const std::string deviceSessionId =
                    idGenerator.nextDeviceSessionId();
                const std::string accessToken = tokenProvider.issueAccessToken(
                    userRecord->userId,
                    deviceSessionId);
                const std::string refreshToken =
                    tokenProvider.issueRefreshToken();
                const std::string refreshTokenHash =
                    tokenProvider.hashOpaqueToken(refreshToken);

                auto loginState =
                    std::make_shared<protocol::dto::auth::LoginResultView>();
                loginState->user.userId = userRecord->userId;
                loginState->user.nickname = userRecord->nickname;
                loginState->user.avatarUrl = userRecord->avatarUrl;
                loginState->deviceSessionId = deviceSessionId;
                loginState->accessToken = accessToken;
                loginState->refreshToken = refreshToken;
                loginState->expiresInSec =
                    tokenProvider.accessTokenExpiresInSec();

                repository::CreateDeviceSessionParams sessionParams;
                sessionParams.deviceSessionId = deviceSessionId;
                sessionParams.userId = userRecord->userId;
                sessionParams.deviceId = request.deviceId;
                sessionParams.devicePlatform = request.devicePlatform;
                sessionParams.deviceName = request.deviceName;
                sessionParams.clientVersion = request.clientVersion;
                sessionParams.loginIp = context.loginIp;
                sessionParams.loginUserAgent = context.userAgent;
                sessionParams.refreshTokenHash = refreshTokenHash;
                sessionParams.refreshTokenExpiresInSec =
                    tokenProvider.refreshTokenExpiresInSec();

                deviceSessionRepository_.createActiveSession(
                    std::move(sessionParams),
                    [loginState, sharedSuccess](
                        repository::CreatedDeviceSessionRecord record) mutable {
                        CHATSERVER_LOG_INFO(kAuthLoginLogTag)
                            << "login succeeded user_id="
                            << loginState->user.userId
                            << " device_session_id="
                            << loginState->deviceSessionId;
                        (*sharedSuccess)(std::move(*loginState));
                    },
                    [sharedFailure](
                        repository::CreateDeviceSessionError error) mutable {
                        if (error.kind ==
                            repository::CreateDeviceSessionErrorKind::
                                kDeviceAlreadyLoggedIn)
                        {
                            CHATSERVER_LOG_WARN(kAuthLoginLogTag)
                                << "login rejected because the device is already logged in";
                            (*sharedFailure)(ServiceError{
                                protocol::error::ErrorCode::
                                    kDeviceAlreadyLoggedIn,
                                "device already logged in",
                            });
                            return;
                        }

                        CHATSERVER_LOG_ERROR(kAuthLoginLogTag)
                            << "login failed while creating device session: "
                            << error.message;
                        (*sharedFailure)(ServiceError{
                            protocol::error::ErrorCode::kInternalError,
                            "failed to create device session",
                        });
                    });
            }
            catch (const std::exception &exception)
            {
                CHATSERVER_LOG_ERROR(kAuthLoginLogTag)
                    << "login failed before creating session: "
                    << exception.what();
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInternalError,
                    "failed to issue login tokens",
                });
            }
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kAuthLoginLogTag)
                << "login failed while querying user: " << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query user",
            });
        });
}

void AuthService::logoutUser(std::string accessToken,
                             LogoutSuccess &&onSuccess,
                             LogoutFailure &&onFailure) const
{
    // 和登录一样，登出也会走异步 repository 回调。
    // 这里先把 success / failure 收口成 shared callback，避免后续多个分支重复 move。
    auto sharedSuccess =
        std::make_shared<LogoutSuccess>(std::move(onSuccess));
    auto sharedFailure =
        std::make_shared<LogoutFailure>(std::move(onFailure));

    accessToken = trimCopy(accessToken);
    if (accessToken.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    infra::security::TokenProvider tokenProvider;
    infra::security::AccessTokenClaims claims;
    if (!tokenProvider.verifyAccessToken(accessToken, &claims))
    {
        CHATSERVER_LOG_WARN(kAuthLogoutLogTag)
            << "logout rejected because access token verification failed";
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    repository::RevokeDeviceSessionParams revokeParams;
    revokeParams.userId = claims.userId;
    revokeParams.deviceSessionId = claims.deviceSessionId;
    revokeParams.revokeReason = "logout";

    // 当前登出语义是：
    // 1) access token 合法 -> 定位唯一 device_session；
    // 2) active -> 改成 revoked/logout；
    // 3) 已经不是 active -> 按幂等成功处理；
    // 4) 找不到 device_session -> 视为无效访问令牌。
    deviceSessionRepository_.revokeSession(
        std::move(revokeParams),
        [claims,
         sharedSuccess,
         sharedFailure](
            repository::RevokeDeviceSessionResult result) mutable {
            if (result.status ==
                repository::RevokeDeviceSessionStatus::kNotFound)
            {
                CHATSERVER_LOG_WARN(kAuthLogoutLogTag)
                    << "logout rejected because device session was not found"
                    << " user_id=" << claims.userId
                    << " device_session_id=" << claims.deviceSessionId;
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInvalidAccessToken,
                    "invalid access token",
                });
                return;
            }

            if (result.status ==
                repository::RevokeDeviceSessionStatus::kRevoked)
            {
                CHATSERVER_LOG_INFO(kAuthLogoutLogTag)
                    << "logout succeeded user_id=" << claims.userId
                    << " device_session_id=" << claims.deviceSessionId;
            }
            else
            {
                CHATSERVER_LOG_INFO(kAuthLogoutLogTag)
                    << "logout treated as idempotent success for inactive session"
                    << " user_id=" << claims.userId
                    << " device_session_id=" << claims.deviceSessionId;
            }

            (*sharedSuccess)();
        },
        [claims,
         sharedFailure](
            repository::RevokeDeviceSessionError error) mutable {
            CHATSERVER_LOG_ERROR(kAuthLogoutLogTag)
                << "logout failed while revoking device session: "
                << error.message
                << " user_id=" << claims.userId
                << " device_session_id=" << claims.deviceSessionId;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to revoke device session",
            });
        });
}

bool AuthService::validateRegisterRequest(
    protocol::dto::auth::RegisterRequest &request,
    ServiceError &error) const
{
    // account 当前要求“原值干净”：
    // - 不帮用户自动 trim；
    // - 前后空格直接视为非法输入。
    //
    // 这样可以避免数据库里同时出现“alice”和“ alice ”这类肉眼难分的账号。
    const std::string trimmedAccount = trimCopy(request.account);
    if (trimmedAccount != request.account)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "account must not contain leading or trailing spaces";
        return false;
    }

    if (request.account.size() < 3 || request.account.size() > 64)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "account length must be between 3 and 64";
        return false;
    }

    if (!std::all_of(request.account.begin(),
                     request.account.end(),
                     isAccountCharacterAllowed))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message =
            "account may contain only letters, digits, '_', '.' and '-'";
        return false;
    }

    // password 当前先只做长度约束，不做复杂度策略。
    // 这是因为第一版重点是打通注册闭环，后续可以再补大小写 / 数字 / 特殊字符策略。
    if (request.password.size() < 8 || request.password.size() > 72)
    {
        // bcrypt 只处理前 72 字节明文。
        // 这里直接把密码长度限制在 8~72，避免前后行为不一致。
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "password length must be between 8 and 72";
        return false;
    }

    // nickname 允许用户输入前后空格，但不会把空白昵称写进数据库。
    // 因此这里先 trim，再做非空和长度判断，并把规范化后的结果写回 request。
    request.nickname = trimCopy(request.nickname);
    if (request.nickname.empty() || request.nickname.size() > 64)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "nickname length must be between 1 and 64";
        return false;
    }

    if (request.avatarUrl.has_value())
    {
        // avatar_url 当前只做轻量规范化：
        // - trim 后为空则按未提供处理；
        // - 不在这里校验 URL scheme 或域名合法性，后续如果接文件服务再加更细规则。
        auto avatarUrl = trimCopy(*request.avatarUrl);
        if (avatarUrl.empty())
        {
            request.avatarUrl.reset();
        }
        else if (avatarUrl.size() > 2048)
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "avatar_url length must not exceed 2048";
            return false;
        }
        else
        {
            request.avatarUrl = std::move(avatarUrl);
        }
    }

    return true;
}

bool AuthService::validateLoginRequest(protocol::dto::auth::LoginRequest &request,
                                       ServiceError &error) const
{
    const std::string trimmedAccount = trimCopy(request.account);
    if (trimmedAccount != request.account)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "account must not contain leading or trailing spaces";
        return false;
    }

    if (request.account.size() < 3 || request.account.size() > 64)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "account length must be between 3 and 64";
        return false;
    }

    if (!std::all_of(request.account.begin(),
                     request.account.end(),
                     isAccountCharacterAllowed))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message =
            "account may contain only letters, digits, '_', '.' and '-'";
        return false;
    }

    if (request.password.empty())
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "password must not be empty";
        return false;
    }

    const std::string trimmedDeviceId = trimCopy(request.deviceId);
    if (trimmedDeviceId.empty() || trimmedDeviceId != request.deviceId ||
        request.deviceId.size() > 128)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message =
            "device_id must not be empty, contain leading/trailing spaces or exceed 128 characters";
        return false;
    }

    request.devicePlatform = trimCopy(request.devicePlatform);
    if (request.devicePlatform.empty() || request.devicePlatform.size() > 32)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "device_platform length must be between 1 and 32";
        return false;
    }

    if (!std::all_of(request.devicePlatform.begin(),
                     request.devicePlatform.end(),
                     isDevicePlatformCharacterAllowed))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message =
            "device_platform may contain only letters, digits, '_' and '-'";
        return false;
    }

    std::transform(request.devicePlatform.begin(),
                   request.devicePlatform.end(),
                   request.devicePlatform.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });

    if (request.deviceName.has_value())
    {
        auto deviceName = trimCopy(*request.deviceName);
        if (deviceName.empty())
        {
            request.deviceName.reset();
        }
        else if (deviceName.size() > 128)
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "device_name length must not exceed 128";
            return false;
        }
        else
        {
            request.deviceName = std::move(deviceName);
        }
    }

    if (request.clientVersion.has_value())
    {
        auto clientVersion = trimCopy(*request.clientVersion);
        if (clientVersion.empty())
        {
            request.clientVersion.reset();
        }
        else if (clientVersion.size() > 32)
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "client_version length must not exceed 32";
            return false;
        }
        else
        {
            request.clientVersion = std::move(clientVersion);
        }
    }

    return true;
}

}  // namespace chatserver::service
