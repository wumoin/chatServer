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

drogon::orm::DbClientPtr DeviceSessionRepository::dbClient() const
{
    return drogon::app().getDbClient();
}

}  // namespace chatserver::repository
