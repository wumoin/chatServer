#pragma once

#include <drogon/orm/DbClient.h>

#include <functional>
#include <optional>
#include <string>

namespace chatserver::repository {

/**
 * @brief 创建或替换当前设备活跃会话所需的参数。
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

class DeviceSessionRepository
{
  public:
    using CreateSessionSuccess =
        std::function<void(CreatedDeviceSessionRecord)>;
    using RepositoryFailure = std::function<void(std::string)>;

    /**
     * @brief 为指定用户创建新的活跃设备会话，并失效同设备上的旧活跃会话。
     * @param params 待写入 `device_sessions` 的会话参数。
     * @param onSuccess 创建成功后的回调。
     * @param onFailure 数据库失败后的回调。
     */
    void createOrReplaceActiveSession(CreateDeviceSessionParams params,
                                      CreateSessionSuccess &&onSuccess,
                                      RepositoryFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return Drogon 默认数据库客户端。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository
