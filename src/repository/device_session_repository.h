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

/**
 * @brief 失效指定设备会话所需的参数。
 */
struct RevokeDeviceSessionParams
{
    std::string userId;
    std::string deviceSessionId;
    std::string revokeReason;
};

enum class RevokeDeviceSessionStatus
{
    kRevoked,
    kAlreadyInactive,
    kNotFound,
};

/**
 * @brief 设备会话失效后的最小结果。
 */
struct RevokeDeviceSessionResult
{
    RevokeDeviceSessionStatus status{RevokeDeviceSessionStatus::kNotFound};
};

struct RevokeDeviceSessionError
{
    std::string message;
};

/**
 * @brief 查询当前仍处于 active 状态的设备会话所需参数。
 */
struct FindActiveDeviceSessionParams
{
    std::string userId;
    std::string deviceSessionId;
    std::string deviceId;
};

/**
 * @brief 当前有效设备会话的最小读取结果。
 */
struct ActiveDeviceSessionRecord
{
    std::string userId;
    std::string deviceSessionId;
    std::string deviceId;
};

class DeviceSessionRepository
{
  public:
    using CreateSessionSuccess =
        std::function<void(CreatedDeviceSessionRecord)>;
    using CreateSessionFailure =
        std::function<void(CreateDeviceSessionError)>;
    using RevokeSessionSuccess =
        std::function<void(RevokeDeviceSessionResult)>;
    using RevokeSessionFailure =
        std::function<void(RevokeDeviceSessionError)>;
    using FindActiveSessionSuccess =
        std::function<void(std::optional<ActiveDeviceSessionRecord>)>;
    using RepositoryFailure = std::function<void(std::string)>;

    /**
     * @brief 为指定用户在当前设备上创建新的活跃会话。
     * @param params 待写入 `device_sessions` 的会话参数。
     * @param onSuccess 创建成功后的回调。
     * @param onFailure 数据库失败后的回调。
     */
    void createActiveSession(CreateDeviceSessionParams params,
                             CreateSessionSuccess &&onSuccess,
                             CreateSessionFailure &&onFailure) const;

    /**
     * @brief 失效指定设备会话。
     * @param params 待失效的设备会话参数。
     * @param onSuccess 更新完成后的回调，返回会话最终状态。
     * @param onFailure 数据库异常后的回调。
     */
    void revokeSession(RevokeDeviceSessionParams params,
                       RevokeSessionSuccess &&onSuccess,
                       RevokeSessionFailure &&onFailure) const;

    /**
     * @brief 查询指定用户当前设备会话是否仍处于 active 状态。
     * @param params 待查询的用户 / 设备会话 / 设备标识。
     * @param onSuccess 查询成功后的回调；若不存在活跃会话则返回空值。
     * @param onFailure 查询失败后的回调，参数为数据库错误文本。
     */
    void findActiveSession(FindActiveDeviceSessionParams params,
                           FindActiveSessionSuccess &&onSuccess,
                           RepositoryFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return Drogon 默认数据库客户端。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository
