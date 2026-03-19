#pragma once

#include "protocol/dto/ws/ws_auth_dto.h"
#include "repository/device_session_repository.h"
#include "service/service_error.h"

#include <drogon/WebSocketConnection.h>

#include <functional>
#include <optional>
#include <string>

namespace chatserver::service {

/**
 * @brief 已认证 WebSocket 连接绑定信息。
 */
struct WsConnectionContext
{
    std::string userId;
    std::string deviceSessionId;
    std::string deviceId;
};

/**
 * @brief WebSocket 会话最小业务编排层。
 *
 * 第一版职责刻意收紧，只处理：
 * 1) `ws.auth` 校验；
 * 2) 连接与用户 / 设备会话绑定；
 * 3) 连接关闭后的解绑。
 */
class WsSessionService
{
  public:
    using AuthenticateSuccess = std::function<void(WsConnectionContext)>;
    using AuthenticateFailure = std::function<void(ServiceError)>;

    /**
     * @brief 校验 `ws.auth` 请求并绑定当前连接。
     * @param request `ws.auth.payload` 对应的请求对象。
     * @param connection 当前 WebSocket 连接。
     * @param onSuccess 认证成功后的回调，返回连接绑定信息。
     * @param onFailure 认证失败后的回调，返回统一业务错误。
     */
    void authenticateConnection(
        protocol::dto::ws::WsAuthRequest request,
        const drogon::WebSocketConnectionPtr &connection,
        AuthenticateSuccess &&onSuccess,
        AuthenticateFailure &&onFailure) const;

    /**
     * @brief 处理 WebSocket 连接关闭后的解绑。
     * @param connection 已关闭的连接对象。
     */
    void handleConnectionClosed(
        const drogon::WebSocketConnectionPtr &connection) const;

    /**
     * @brief 判断指定连接是否已经完成 `ws.auth`。
     * @param connection 当前 WebSocket 连接。
     * @return true 表示已认证；false 表示仍是匿名连接。
     */
    bool isAuthenticated(
        const drogon::WebSocketConnectionPtr &connection) const;

    /**
     * @brief 读取当前连接绑定的认证上下文。
     * @param connection 当前 WebSocket 连接。
     * @return 存在时返回连接上下文；否则返回空值。
     */
    std::optional<WsConnectionContext> connectionContext(
        const drogon::WebSocketConnectionPtr &connection) const;

  private:
    repository::DeviceSessionRepository deviceSessionRepository_;
};

}  // namespace chatserver::service
