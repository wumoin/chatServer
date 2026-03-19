#pragma once

#include <drogon/WebSocketConnection.h>

#include <string>
#include <vector>

namespace chatserver::infra::ws {

/**
 * @brief 在线连接注册参数。
 */
struct ConnectionBinding
{
    std::string userId;
    std::string deviceSessionId;
    std::string deviceId;
    drogon::WebSocketConnectionPtr connection;
};

/**
 * @brief 当前进程内的在线 WebSocket 连接注册表。
 *
 * 它不是数据库表，而是进程内的在线连接索引：
 * 1) `ws.auth` 成功后注册连接；
 * 2) 连接关闭时移除连接；
 * 3) 后续主动推消息时按用户查找连接。
 */
class ConnectionRegistry
{
  public:
    /**
     * @brief 注册或替换指定设备会话对应的在线连接。
     * @param binding 当前连接与用户 / 设备会话的绑定关系。
     */
    static void registerConnection(ConnectionBinding binding);

    /**
     * @brief 移除指定设备会话的在线连接。
     * @param deviceSessionId 设备会话 ID。
     * @param connection 当前关闭的连接对象，用于避免误删新连接。
     */
    static void unregisterConnection(
        const std::string &deviceSessionId,
        const drogon::WebSocketConnectionPtr &connection);

    /**
     * @brief 查询指定用户当前在线的连接。
     * @param userId 用户 ID。
     * @return 当前仍有效的连接列表。
     */
    static std::vector<drogon::WebSocketConnectionPtr> findConnectionsByUserId(
        const std::string &userId);
};

}  // namespace chatserver::infra::ws
