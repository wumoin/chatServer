#pragma once

#include <drogon/orm/DbClient.h>

#include <functional>
#include <optional>
#include <string>

namespace chatserver::repository {

/**
 * @brief 创建当前设备活跃会话所需的参数。
 */
struct CreateDeviceSessionParams
{
    std::string deviceSessionId;
    std::string userId;
    std::string deviceId;
    std::string devicePlatform;
    std::optional<std::string> deviceName;
    std::optional<std::string> clientVersion;
    std::string loginIp;
    std::optional<std::string> loginUserAgent;
    std::string refreshTokenHash;
    std::int64_t refreshTokenExpiresInSec{0};
};

/**
 * @brief 设备会话创建成功后的最小回传结果。
 */
struct CreatedDeviceSessionRecord
{
    std::string deviceSessionId;
};

enum class CreateDeviceSessionErrorKind
{
    kDeviceAlreadyLoggedIn,
    kDatabaseError,
};

struct CreateDeviceSessionError
{
    CreateDeviceSessionErrorKind kind{
        CreateDeviceSessionErrorKind::kDatabaseError};
    std::string message;
};

class DeviceSessionRepository
{
  public:
    using CreateSessionSuccess =
        std::function<void(CreatedDeviceSessionRecord)>;
    using CreateSessionFailure =
        std::function<void(CreateDeviceSessionError)>;

    /**
     * @brief 为指定用户在当前设备上创建新的活跃会话。
     * @param params 待写入 `device_sessions` 的会话参数。
     * @param onSuccess 创建成功后的回调。
     * @param onFailure 数据库失败后的回调。
     */
    void createActiveSession(CreateDeviceSessionParams params,
                             CreateSessionSuccess &&onSuccess,
                             CreateSessionFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return Drogon 默认数据库客户端。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository
