#include "service/auth_service.h"

#include "infra/id/id_generator.h"
#include "infra/security/password_hasher.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chatserver::service {
namespace {

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
                    LOG_WARN << "Register rejected because account already "
                                "exists";
                    break;
                case repository::CreateUserErrorKind::kDatabaseError:
                default:
                    serviceError.code =
                        protocol::error::ErrorCode::kInternalError;
                    serviceError.message = "failed to create user";
                    LOG_ERROR << "Register failed while inserting user: "
                              << error.message;
                    break;
                }

                onFailure(std::move(serviceError));
            });
    }
    catch (const std::exception &exception)
    {
        LOG_ERROR << "Register failed before database insert: "
                  << exception.what();

        onFailure(ServiceError{
            protocol::error::ErrorCode::kInternalError,
            "failed to hash password",
        });
    }
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

}  // namespace chatserver::service
