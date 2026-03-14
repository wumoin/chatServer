#pragma once

#include <cstdint>
#include <string>

namespace chatserver::infra::security {

/**
 * @brief 访问令牌中的最小 claims。
 */
struct AccessTokenClaims
{
    std::string userId;
    std::string deviceSessionId;
    std::int64_t issuedAtSec{0};
    std::int64_t expiresAtSec{0};
};

/**
 * @brief 统一负责 access token / refresh token 的生成与校验。
 *
 * 当前目标不是引入完整 JWT 生态，而是先提供一套项目内可控、可验证的最小令牌能力：
 * 1. access token 可自包含 user_id / device_session_id / 过期时间；
 * 2. refresh token 仍然是随机串，但数据库只存哈希值；
 * 3. 后续 WebSocket `ws.auth` 和 refresh 接口可以复用同一套实现。
 */
class TokenProvider
{
  public:
    /**
     * @brief 从 `app.json` 初始化令牌配置。
     * @param configPath 配置文件绝对路径。
     */
    static void initialize(const std::string &configPath);

    /**
     * @brief 判断令牌模块是否已经初始化完成。
     * @return true 表示已初始化；false 表示尚未初始化。
     */
    static bool isInitialized();

    /**
     * @brief 生成访问令牌。
     * @param userId 登录用户 ID。
     * @param deviceSessionId 当前设备会话 ID。
     * @return 可直接回给客户端的访问令牌字符串。
     */
    std::string issueAccessToken(const std::string &userId,
                                 const std::string &deviceSessionId) const;

    /**
     * @brief 校验访问令牌并解析 claims。
     * @param token 客户端提交的访问令牌。
     * @param claims 校验成功时写入解析后的 claims，可为空。
     * @return true 表示签名和结构都合法；false 表示非法或已损坏。
     */
    bool verifyAccessToken(const std::string &token,
                           AccessTokenClaims *claims = nullptr) const;

    /**
     * @brief 生成原始 refresh token。
     * @return 返回给客户端保存的 refresh token 原文。
     */
    std::string issueRefreshToken() const;

    /**
     * @brief 计算 refresh token 的哈希值。
     * @param token 原始 refresh token。
     * @return 可写入数据库的哈希值。
     */
    std::string hashOpaqueToken(const std::string &token) const;

    /**
     * @brief 返回 access token 的有效时长。
     * @return 秒数。
     */
    std::int64_t accessTokenExpiresInSec() const;

    /**
     * @brief 返回 refresh token 的有效时长。
     * @return 秒数。
     */
    std::int64_t refreshTokenExpiresInSec() const;
};

}  // namespace chatserver::infra::security
