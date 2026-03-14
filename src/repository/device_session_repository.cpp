#include "repository/device_session_repository.h"

#include <drogon/drogon.h>

#include <exception>
#include <string>
#include <utility>

namespace chatserver::repository {
namespace {

constexpr auto kCreateOrReplaceSessionSql = R"SQL(
WITH revoke_existing AS (
    UPDATE device_sessions
    SET session_status = 'revoked',
        revoked_at = NOW(),
        revoke_reason = 'relogin'
    WHERE user_id = $1
      AND device_id = $2
      AND session_status = 'active'
),
touch_user AS (
    UPDATE users
    SET last_login_at = NOW()
    WHERE user_id = $1
    RETURNING user_id
)
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
VALUES (
    $3,
    $1,
    $2,
    $4,
    $5,
    $6,
    NULLIF($7, '')::INET,
    $8,
    $9,
    NOW() + ($10 * INTERVAL '1 second')
)
RETURNING device_session_id
)SQL";

}  // namespace

void DeviceSessionRepository::createOrReplaceActiveSession(
    CreateDeviceSessionParams params,
    CreateSessionSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();

        client->execSqlAsync(
            kCreateOrReplaceSessionSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(CreatedDeviceSessionRecord{});
                    return;
                }

                CreatedDeviceSessionRecord record;
                record.deviceSessionId =
                    rows[0]["device_session_id"].as<std::string>();
                onSuccess(std::move(record));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
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
        onFailure(exception.what());
    }
}

drogon::orm::DbClientPtr DeviceSessionRepository::dbClient() const
{
    return drogon::app().getDbClient();
}

}  // namespace chatserver::repository
