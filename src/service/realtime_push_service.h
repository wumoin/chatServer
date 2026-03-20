#pragma once

#include "protocol/error/error_code.h"

#include <drogon/WebSocketConnection.h>
#include <json/value.h>

#include <string>
#include <vector>

namespace chatserver::service {

/**
 * @brief WebSocket 实时推送服务。
 *
 * 这一层只负责把已经确定要通知客户端的业务结果推送到在线连接：
 * - 不负责权限判断；
 * - 不负责数据库写入；
 * - 不负责业务事务控制。
 */
class RealtimePushService
{
  public:
    /**
     * @brief 向指定连接推送 `ws.ack`。
     * @param connection 目标连接。
     * @param requestId 本次确认对应的请求 ID。
     * @param route 对应的业务路由。
     * @param ok 本次动作是否成功。
     * @param code 业务错误码。
     * @param message 结果说明。
     * @param data 可选业务返回值。
     */
    void pushAckToConnection(const drogon::WebSocketConnectionPtr &connection,
                             const std::string &requestId,
                             const std::string &route,
                             bool ok,
                             protocol::error::ErrorCode code,
                             std::string message,
                             Json::Value data = Json::Value(
                                 Json::objectValue)) const;

    /**
     * @brief 按设备会话推送 `ws.ack`。
     * @param deviceSessionId 目标设备会话 ID。
     * @param requestId 本次确认对应的请求 ID。
     * @param route 对应的业务路由。
     * @param ok 本次动作是否成功。
     * @param code 业务错误码。
     * @param message 结果说明。
     * @param data 可选业务返回值。
     */
    void pushAckToDeviceSession(const std::string &deviceSessionId,
                                const std::string &requestId,
                                const std::string &route,
                                bool ok,
                                protocol::error::ErrorCode code,
                                std::string message,
                                Json::Value data = Json::Value(
                                    Json::objectValue)) const;

    /**
     * @brief 向指定连接推送 `ws.new`。
     * @param connection 目标连接。
     * @param route 当前新事件的业务路由。
     * @param data 业务载荷。
     * @param requestId 可选请求 ID，主动推送时通常为空。
     */
    void pushNewToConnection(const drogon::WebSocketConnectionPtr &connection,
                             const std::string &route,
                             Json::Value data,
                             const std::string &requestId = "") const;

    /**
     * @brief 向指定设备会话推送 `ws.new`。
     * @param deviceSessionId 目标设备会话 ID。
     * @param route 当前新事件的业务路由。
     * @param data 业务载荷。
     * @param requestId 可选请求 ID，主动推送时通常为空。
     */
    void pushNewToDeviceSession(const std::string &deviceSessionId,
                                const std::string &route,
                                Json::Value data,
                                const std::string &requestId = "") const;

    /**
     * @brief 向指定用户的全部在线连接推送 `ws.new`。
     * @param userId 目标用户 ID。
     * @param route 当前新事件的业务路由。
     * @param data 业务载荷。
     * @param requestId 可选请求 ID，主动推送时通常为空。
     */
    void pushNewToUser(const std::string &userId,
                       const std::string &route,
                       Json::Value data,
                       const std::string &requestId = "") const;

    /**
     * @brief 向一组用户的全部在线连接推送 `ws.new`。
     * @param userIds 目标用户 ID 列表。
     * @param route 当前新事件的业务路由。
     * @param data 业务载荷。
     * @param requestId 可选请求 ID，主动推送时通常为空。
     */
    void pushNewToUsers(const std::vector<std::string> &userIds,
                        const std::string &route,
                        Json::Value data,
                        const std::string &requestId = "") const;
};

}  // namespace chatserver::service
