#pragma once

#include "repository/conversation_repository.h"
#include "service/realtime_push_service.h"
#include "service/ws_session_service.h"

#include <drogon/WebSocketConnection.h>
#include <json/value.h>

#include <string>

namespace chatserver::service {

/**
 * @brief WebSocket 文本消息业务编排层。
 *
 * 第一版只处理：
 * 1) `ws.send + route=message.send_text`；
 * 2) 当前连接成员权限校验；
 * 3) 文本消息入库；
 * 4) 回发 `ws.ack`；
 * 5) 推送 `ws.new + route=message.created`。
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

  private:
    repository::ConversationRepository conversationRepository_;
    RealtimePushService realtimePushService_;
};

}  // namespace chatserver::service
