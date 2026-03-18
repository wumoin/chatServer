#include "repository/user_repository.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chatserver::repository {
namespace {

constexpr auto kInsertUserSql = R"SQL(
INSERT INTO users (
    user_id,
    account,
    nickname,
    avatar_url,
    password_hash,
    password_algo
)
VALUES ($1, $2, $3, $4, $5, $6)
RETURNING
    user_id,
    account,
    nickname,
    avatar_url,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
)SQL";

constexpr auto kFindUserByAccountSql = R"SQL(
SELECT
    user_id,
    account,
    nickname,
    avatar_url,
    password_hash,
    password_algo,
    account_status
FROM users
WHERE account = $1
LIMIT 1
)SQL";

constexpr auto kFindUserByIdSql = R"SQL(
SELECT
    user_id,
    account,
    nickname,
    avatar_url
FROM users
WHERE user_id = $1
LIMIT 1
)SQL";

constexpr auto kUpdateUserProfileSql = R"SQL(
UPDATE users
SET
    nickname = CASE WHEN $2 THEN $3::VARCHAR(64) ELSE nickname END,
    avatar_url = CASE WHEN $4 THEN $5::TEXT ELSE avatar_url END
WHERE user_id = $1
RETURNING
    user_id,
    account,
    nickname,
    avatar_url
)SQL";

bool isDuplicateAccountError(const drogon::orm::DrogonDbException &exception)
{
    // Drogon 在不同数据库驱动、不同版本下，对唯一约束异常的具体类型封装并不完全一致。
    // 因此这里做三层兜底识别：
    // 1) 优先识别 UniqueViolation；
    // 2) 再识别 PostgreSQL SQLSTATE 23505；
    // 3) 最后再按错误消息做保底匹配。
    //
    // 这样可以先把“账号已存在 -> 409”这条业务行为稳定住，后续若升级 ORM 再统一收紧。
    const auto *uniqueViolation =
        dynamic_cast<const drogon::orm::UniqueViolation *>(&exception.base());
    if (uniqueViolation != nullptr)
    {
        return true;
    }

    const auto *sqlError =
        dynamic_cast<const drogon::orm::SqlError *>(&exception.base());
    if (sqlError != nullptr && sqlError->sqlState() == "23505")
    {
        return true;
    }

    const std::string_view message = exception.base().what();
    return message.find("duplicate key value violates unique constraint") !=
           std::string_view::npos;
}

}  // namespace

void UserRepository::createUser(CreateUserParams params,
                                CreateUserSuccess &&onSuccess,
                                CreateUserFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();

        // 失败回调可能被多个异步分支共用：
        // - SQL 执行报错
        // - SQL 成功但 RETURNING 结果异常
        //
        // 这里用 shared_ptr 包起来，避免 move-only callback 被某个分支提前消费。
        auto sharedFailure =
            std::make_shared<CreateUserFailure>(std::move(onFailure));

        client->execSqlAsync(
            kInsertUserSql,
            [onSuccess = std::move(onSuccess),
             sharedFailure](
                const drogon::orm::Result &rows) mutable {
                // 正常情况下 INSERT ... RETURNING 至少会带回 1 行。
                // 如果没有结果，说明数据库行为与预期不一致，按数据库错误处理更稳妥。
                if (rows.empty())
                {
                    (*sharedFailure)(CreateUserError{
                        CreateUserErrorKind::kDatabaseError,
                        "insert users returned no rows",
                    });
                    return;
                }

                const auto &row = rows[0];

                // repository 层把数据库返回的 Row 转成上层真正需要的结构。
                // 这里只提取注册成功响应必须使用到的字段。
                CreatedUserRecord record;
                record.userId = row["user_id"].as<std::string>();
                record.account = row["account"].as<std::string>();
                record.nickname = row["nickname"].as<std::string>();
                if (!row["avatar_url"].isNull())
                {
                    record.avatarUrl = row["avatar_url"].as<std::string>();
                }
                record.createdAtMs = row["created_at_ms"].as<std::int64_t>();

                onSuccess(std::move(record));
            },
            [sharedFailure](const drogon::orm::DrogonDbException &exception) mutable {
                CreateUserError error;
                if (isDuplicateAccountError(exception))
                {
                    // 唯一约束冲突交给 service 映射成“账号已存在”，而不是通用 500。
                    error.kind = CreateUserErrorKind::kAccountAlreadyExists;
                    error.message = exception.base().what();
                }
                else
                {
                    // 其它数据库问题继续保留为内部错误，由 service 统一打日志和降级。
                    error.kind = CreateUserErrorKind::kDatabaseError;
                    error.message = exception.base().what();
                }

                (*sharedFailure)(std::move(error));
            },
            params.userId,
            params.account,
            params.nickname,
            params.avatarUrl,
            params.passwordHash,
            params.passwordAlgo);
    }
    catch (const std::exception &exception)
    {
        // getDbClient() 或参数绑定阶段也可能在进入异步 SQL 前就抛异常。
        // 这类错误同样按数据库错误路径往上抛。
        onFailure(CreateUserError{CreateUserErrorKind::kDatabaseError,
                                  exception.what()});
    }
}

void UserRepository::findUserByAccount(std::string account,
                                       FindUserByAccountSuccess &&onSuccess,
                                       RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();

        client->execSqlAsync(
            kFindUserByAccountSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(std::nullopt);
                    return;
                }

                const auto &row = rows[0];
                AuthUserRecord record;
                record.userId = row["user_id"].as<std::string>();
                record.account = row["account"].as<std::string>();
                record.nickname = row["nickname"].as<std::string>();
                if (!row["avatar_url"].isNull())
                {
                    record.avatarUrl = row["avatar_url"].as<std::string>();
                }
                record.passwordHash = row["password_hash"].as<std::string>();
                record.passwordAlgo = row["password_algo"].as<std::string>();
                record.accountStatus = row["account_status"].as<std::string>();

                onSuccess(std::move(record));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(account));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void UserRepository::findUserById(std::string userId,
                                  FindUserByIdSuccess &&onSuccess,
                                  RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();

        client->execSqlAsync(
            kFindUserByIdSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(std::nullopt);
                    return;
                }

                const auto &row = rows[0];
                UserProfileRecord record;
                record.userId = row["user_id"].as<std::string>();
                record.account = row["account"].as<std::string>();
                record.nickname = row["nickname"].as<std::string>();
                if (!row["avatar_url"].isNull())
                {
                    record.avatarUrl = row["avatar_url"].as<std::string>();
                }

                onSuccess(std::move(record));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(userId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void UserRepository::updateUserProfile(UpdateUserProfileParams params,
                                       UpdateUserProfileSuccess &&onSuccess,
                                       RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();

        client->execSqlAsync(
            kUpdateUserProfileSql,
            [onSuccess = std::move(onSuccess),
             onFailure = std::move(onFailure)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onFailure("update users returned no rows");
                    return;
                }

                const auto &row = rows[0];
                UserProfileRecord record;
                record.userId = row["user_id"].as<std::string>();
                record.account = row["account"].as<std::string>();
                record.nickname = row["nickname"].as<std::string>();
                if (!row["avatar_url"].isNull())
                {
                    record.avatarUrl = row["avatar_url"].as<std::string>();
                }

                onSuccess(std::move(record));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            params.userId,
            params.updateNickname,
            params.nickname,
            params.updateAvatarUrl,
            params.avatarUrl);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

drogon::orm::DbClientPtr UserRepository::dbClient() const
{
    // 当前统一使用 Drogon 的默认数据库客户端。
    // 对应要求是 app.json 中的 db_clients 默认项继续保持 name="default"。
    return drogon::app().getDbClient();
}

}  // namespace chatserver::repository
