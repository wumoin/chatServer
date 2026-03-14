#include "repository/device_session_repository.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace chatserver::repository {
namespace {

constexpr auto kCreateSessionSql = R"SQL(
WITH inserted AS (
    INSERT INTO device_sessions (
        device_session_id,
        user_id,
        device_id,
        device_platform,
        device_name,
        client_version,
        login_ip,
        login_user_agent,
        refresh_token_hash,
        refresh_token_expires_at
    )
    SELECT
        $3::VARCHAR(32),
        $1::VARCHAR(32),
        $2::VARCHAR(128),
        $4::VARCHAR(32),
        $5::VARCHAR(128),
        $6::VARCHAR(32),
        NULLIF($7, '')::INET,
        $8,
        $9,
        NOW() + ($10 * INTERVAL '1 second')
    WHERE NOT EXISTS (
        SELECT 1
        FROM device_sessions
        WHERE user_id = $1::VARCHAR(32)
          AND device_id = $2::VARCHAR(128)
          AND session_status = 'active'
    )
    RETURNING device_session_id
),
touch_user AS (
    UPDATE users
    SET last_login_at = NOW()
    WHERE user_id = $1::VARCHAR(32)
      AND EXISTS (SELECT 1 FROM inserted)
    RETURNING user_id
)
SELECT device_session_id
FROM inserted
)SQL";

constexpr auto kRevokeSessionSql = R"SQL(
WITH target AS (
    SELECT session_status
    FROM device_sessions
    WHERE user_id = $1::VARCHAR(32)
      AND device_session_id = $2::VARCHAR(32)
),
updated AS (
    UPDATE device_sessions
    SET session_status = 'revoked',
        revoked_at = NOW(),
        revoke_reason = $3::VARCHAR(64)
    WHERE user_id = $1::VARCHAR(32)
      AND device_session_id = $2::VARCHAR(32)
      AND session_status = 'active'
    RETURNING device_session_id
)
SELECT
    EXISTS (SELECT 1 FROM target) AS session_exists,
    EXISTS (SELECT 1 FROM updated) AS session_revoked
)SQL";
// 这条 SQL 刻意不直接 DELETE 会话，而是只做状态流转：
// - active -> revoked/logout
// - 已经非 active 时保留原状态，交给上层按幂等成功处理
// - target 不存在时再由 service 判断成“无效访问令牌”

bool isDuplicateActiveDeviceSession(
    const drogon::orm::DrogonDbException &exception)
{
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

void DeviceSessionRepository::createActiveSession(
    CreateDeviceSessionParams params,
    CreateSessionSuccess &&onSuccess,
    CreateSessionFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        auto sharedFailure =
            std::make_shared<CreateSessionFailure>(std::move(onFailure));

        client->execSqlAsync(
            kCreateSessionSql,
            [onSuccess = std::move(onSuccess),
             sharedFailure](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    (*sharedFailure)(CreateDeviceSessionError{
                        CreateDeviceSessionErrorKind::kDeviceAlreadyLoggedIn,
                        "active device session already exists",
                    });
                    return;
                }

                CreatedDeviceSessionRecord record;
                record.deviceSessionId =
                    rows[0]["device_session_id"].as<std::string>();
                onSuccess(std::move(record));
            },
            [sharedFailure](
                const drogon::orm::DrogonDbException &exception) mutable {
                CreateDeviceSessionError error;
                if (isDuplicateActiveDeviceSession(exception))
                {
                    error.kind =
                        CreateDeviceSessionErrorKind::kDeviceAlreadyLoggedIn;
                    error.message = exception.base().what();
                }
                else
                {
                    error.kind = CreateDeviceSessionErrorKind::kDatabaseError;
                    error.message = exception.base().what();
                }

                (*sharedFailure)(std::move(error));
            },
            params.userId,
            params.deviceId,
            params.deviceSessionId,
            params.devicePlatform,
            params.deviceName,
            params.clientVersion,
            params.loginIp,
            params.loginUserAgent,
            params.refreshTokenHash,
            params.refreshTokenExpiresInSec);
    }
    catch (const std::exception &exception)
    {
        onFailure(CreateDeviceSessionError{
            CreateDeviceSessionErrorKind::kDatabaseError,
            exception.what(),
        });
    }
}

void DeviceSessionRepository::revokeSession(
    RevokeDeviceSessionParams params,
    RevokeSessionSuccess &&onSuccess,
    RevokeSessionFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();

        client->execSqlAsync(
            kRevokeSessionSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                RevokeDeviceSessionResult result;
                if (rows.empty())
                {
                    result.status = RevokeDeviceSessionStatus::kNotFound;
                    onSuccess(std::move(result));
                    return;
                }

                const bool sessionExists = rows[0]["session_exists"].as<bool>();
                const bool sessionRevoked = rows[0]["session_revoked"].as<bool>();

                // 三种结果对应三种上层语义：
                // - 不存在：token 对应不到真实会话
                // - 已更新：本次真的完成了登出
                // - 存在但没更新：说明之前已经是非 active，会按幂等成功处理
                if (!sessionExists)
                {
                    result.status = RevokeDeviceSessionStatus::kNotFound;
                }
                else if (sessionRevoked)
                {
                    result.status = RevokeDeviceSessionStatus::kRevoked;
                }
                else
                {
                    result.status = RevokeDeviceSessionStatus::kAlreadyInactive;
                }

                onSuccess(std::move(result));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(RevokeDeviceSessionError{exception.base().what()});
            },
            params.userId,
            params.deviceSessionId,
            params.revokeReason);
    }
    catch (const std::exception &exception)
    {
        onFailure(RevokeDeviceSessionError{exception.what()});
    }
}

drogon::orm::DbClientPtr DeviceSessionRepository::dbClient() const
{
    return drogon::app().getDbClient();
}

}  // namespace chatserver::repository
