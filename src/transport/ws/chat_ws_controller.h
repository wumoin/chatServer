#pragma once

#include "service/realtime_push_service.h"
#include "service/ws_message_service.h"
#include "service/ws_session_service.h"

#include <drogon/WebSocketController.h>

namespace chatserver::transport::ws {

/**
 * @brief 服务端 WebSocket 最小入口控制器。
 *
 * 第一版先只负责：
 * 1) 建连与断连；
 * 2) `ws.auth`；
 * 3) `ws.send` 的基础解析与分发入口；
 * 4) `ws.ack / ws.new` 的基础回包能力；
 * 5) 统一 `ws.error` 回包。
 */
class ChatWsController
    : public drogon::WebSocketController<ChatWsController>
{
  public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws");
    WS_PATH_LIST_END

    /**
     * @brief 处理新建的 WebSocket 连接。
     * @param request 建立 WebSocket 握手时的 HTTP 请求。
     * @param connection 当前连接对象。
     */
    void handleNewConnection(const drogon::HttpRequestPtr &request,
                             const drogon::WebSocketConnectionPtr &connection)
        override;

    /**
     * @brief 处理收到的 WebSocket 消息。
     * @param connection 当前连接对象。
     * @param message 收到的消息文本。
     * @param type 当前消息类型。
     */
    void handleNewMessage(const drogon::WebSocketConnectionPtr &connection,
                          std::string &&message,
                          const drogon::WebSocketMessageType &type) override;

    /**
     * @brief 处理 WebSocket 连接关闭事件。
     * @param connection 已关闭的连接对象。
     */
    void handleConnectionClosed(
        const drogon::WebSocketConnectionPtr &connection) override;

  private:
    service::RealtimePushService realtimePushService_;
    service::WsMessageService wsMessageService_;
    service::WsSessionService wsSessionService_;
};

}  // namespace chatserver::transport::ws
