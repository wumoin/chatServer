#pragma once

#include "repository/conversation_repository.h"
#include "service/file_service.h"
#include "service/realtime_push_service.h"
#include "service/ws_session_service.h"

#include <drogon/WebSocketConnection.h>
#include <json/value.h>

#include <string>

namespace chatserver::service {

/**
 * @brief WebSocket 消息业务编排层。
 *
 * 当前已处理：
 * 1) `ws.send + route=message.send_text`；
 * 2) `ws.send + route=message.send_image`；
 * 3) 当前连接成员权限校验；
 * 4) 正式消息入库；
 * 5) 回发 `ws.ack`；
 * 6) 推送 `ws.new + route=message.created`。
 */
class WsMessageService
{
  public:
    /**
     * @brief 处理一条文本消息发送请求。
     * @param data `ws.send.payload.data`。
     * @param requestId 当前 `ws.send` 的请求 ID。
     * @param context 当前已认证连接的上下文。
     * @param connection 当前 WebSocket 连接。
     */
    void handleSendTextMessage(Json::Value data,
                               std::string requestId,
                               WsConnectionContext context,
                               const drogon::WebSocketConnectionPtr &connection) const;

    /**
     * @brief 处理一条图片消息发送请求。
     * @param data `ws.send.payload.data`。
     * @param requestId 当前 `ws.send` 的请求 ID。
     * @param context 当前已认证连接的上下文。
     * @param connection 当前 WebSocket 连接。
     */
    void handleSendImageMessage(
        Json::Value data,
        std::string requestId,
        WsConnectionContext context,
        const drogon::WebSocketConnectionPtr &connection) const;

  private:
    repository::ConversationRepository conversationRepository_;
    FileService fileService_;
    RealtimePushService realtimePushService_;
};

}  // namespace chatserver::service
