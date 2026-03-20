#include "transport/ws/chat_ws_controller.h"

#include "infra/log/app_logger.h"
#include "protocol/dto/ws/ws_auth_dto.h"
#include "protocol/dto/ws/ws_business_dto.h"
#include "protocol/dto/ws/ws_envelope_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/WebSocketConnection.h>
#include <json/json.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace chatserver::transport::ws {
namespace {

constexpr auto kWsLogTag = "ws";

std::int64_t nowEpochMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

bool parseJsonMessage(const std::string &message,
                      Json::Value &out,
                      std::string &errorMessage)
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const bool ok = reader->parse(message.data(),
                                  message.data() + message.size(),
                                  &out,
                                  &errors);
    if (!ok)
    {
        errorMessage = errors.empty() ? "websocket JSON 消息体无效" : errors;
    }

    return ok;
}

void sendEnvelope(const drogon::WebSocketConnectionPtr &connection,
                  std::string type,
                  std::string requestId,
                  Json::Value payload)
{
    protocol::dto::ws::WsEnvelope envelope;
    envelope.version = 1;
    envelope.type = std::move(type);
    envelope.requestId = std::move(requestId);
    envelope.tsMs = nowEpochMs();
    envelope.payload = std::move(payload);
    connection->sendJson(protocol::dto::ws::toJson(envelope));
}

void sendError(const drogon::WebSocketConnectionPtr &connection,
               const std::string &requestId,
               protocol::error::ErrorCode code,
               std::string message)
{
    protocol::dto::ws::WsErrorPayload payload;
    payload.code = static_cast<int>(code);
    payload.message = std::move(message);
    sendEnvelope(connection,
                 "ws.error",
                 requestId,
                 protocol::dto::ws::toJson(payload));
}

}  // namespace

void ChatWsController::handleNewConnection(
    const drogon::HttpRequestPtr &request,
    const drogon::WebSocketConnectionPtr &connection)
{
    if (connection == nullptr)
    {
        return;
    }

    CHATSERVER_LOG_INFO(kWsLogTag)
        << "实时通道已建立连接，来源=" << request->peerAddr().toIpPort()
        << "，等待 ws.auth";
}

void ChatWsController::handleNewMessage(
    const drogon::WebSocketConnectionPtr &connection,
    std::string &&message,
    const drogon::WebSocketMessageType &type)
{
    if (connection == nullptr)
    {
        return;
    }

    if (type == drogon::WebSocketMessageType::Ping ||
        type == drogon::WebSocketMessageType::Pong)
    {
        return;
    }

    if (type != drogon::WebSocketMessageType::Text)
    {
        sendError(connection,
                  "",
                  protocol::error::ErrorCode::kInvalidArgument,
                  "仅支持文本类型的业务消息");
        connection->shutdown(drogon::CloseCode::kInvalidMessage,
                             "仅支持文本类型的业务消息");
        return;
    }

    Json::Value json;
    std::string parseError;
    if (!parseJsonMessage(message, json, parseError))
    {
        CHATSERVER_LOG_WARN(kWsLogTag)
            << "实时通道收到无效 JSON 消息，原因：" << parseError;
        sendError(connection,
                  "",
                  protocol::error::ErrorCode::kInvalidArgument,
                  "websocket JSON 消息体无效");
        connection->shutdown(drogon::CloseCode::kWrongMessageContent,
                             "websocket JSON 消息体无效");
        return;
    }

    protocol::dto::ws::WsEnvelope envelope;
    if (!protocol::dto::ws::parseWsEnvelope(json, envelope, parseError))
    {
        CHATSERVER_LOG_WARN(kWsLogTag)
            << "实时通道收到无效信封消息，原因：" << parseError;
        sendError(connection,
                  "",
                  protocol::error::ErrorCode::kInvalidArgument,
                  parseError);
        connection->shutdown(drogon::CloseCode::kWrongMessageContent,
                             "websocket 信封无效");
        return;
    }

    if (envelope.type == "ws.auth")
    {
        if (wsSessionService_.isAuthenticated(connection))
        {
            sendError(connection,
                      envelope.requestId,
                      protocol::error::ErrorCode::kInvalidArgument,
                      "当前 WebSocket 连接已完成认证");
            return;
        }

        protocol::dto::ws::WsAuthRequest authRequest;
        if (!protocol::dto::ws::parseWsAuthRequest(envelope.payload,
                                                   authRequest,
                                                   parseError))
        {
            sendError(connection,
                      envelope.requestId,
                      protocol::error::ErrorCode::kInvalidArgument,
                      parseError);
            connection->shutdown(drogon::CloseCode::kViolation,
                                 "ws.auth 载荷无效");
            return;
        }

        wsSessionService_.authenticateConnection(
            std::move(authRequest),
            connection,
            [connection, requestId = envelope.requestId](
                service::WsConnectionContext context) mutable {
                if (context.userId.empty())
                {
                    sendError(connection,
                              requestId,
                              protocol::error::ErrorCode::kInvalidAccessToken,
                              "invalid access token");
                    connection->shutdown(drogon::CloseCode::kViolation,
                                         "invalid access token");
                    return;
                }

                protocol::dto::ws::WsAuthOkPayload payload;
                payload.userId = std::move(context.userId);
                payload.deviceSessionId = std::move(context.deviceSessionId);

                CHATSERVER_LOG_INFO(kWsLogTag)
                    << "ws.auth 已通过，device_session_id="
                    << payload.deviceSessionId;

                sendEnvelope(connection,
                             "ws.auth.ok",
                             requestId,
                             protocol::dto::ws::toJson(payload));
            },
            [connection, requestId = envelope.requestId](
                service::ServiceError error) mutable {
                sendError(connection, requestId, error.code, error.message);
                if (error.code == protocol::error::ErrorCode::kInvalidAccessToken ||
                    error.code == protocol::error::ErrorCode::kForbidden)
                {
                    connection->shutdown(drogon::CloseCode::kViolation,
                                         error.message);
                }
            });
        return;
    }

    if (!wsSessionService_.isAuthenticated(connection))
    {
        sendError(connection,
                  envelope.requestId,
                  protocol::error::ErrorCode::kInvalidAccessToken,
                  "当前 WebSocket 连接尚未完成认证");
        connection->shutdown(drogon::CloseCode::kViolation,
                             "当前 WebSocket 连接尚未完成认证");
        return;
    }

    if (envelope.type == "ws.send")
    {
        protocol::dto::ws::WsSendPayload sendPayload;
        if (!protocol::dto::ws::parseWsSendPayload(envelope.payload,
                                                   sendPayload,
                                                   parseError))
        {
            CHATSERVER_LOG_WARN(kWsLogTag)
                << "实时通道收到无效 ws.send 载荷，原因：" << parseError;
            sendError(connection,
                      envelope.requestId,
                      protocol::error::ErrorCode::kInvalidArgument,
                      parseError);
            return;
        }

        CHATSERVER_LOG_INFO(kWsLogTag)
            << "收到 ws.send，route=" << sendPayload.route
            << " request_id=" << envelope.requestId;

        realtimePushService_.pushAckToConnection(
            connection,
            envelope.requestId,
            sendPayload.route,
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "暂不支持该 WebSocket 业务路由");
        return;
    }

    if (envelope.type == "ws.ack" || envelope.type == "ws.new")
    {
        sendError(connection,
                  envelope.requestId,
                  protocol::error::ErrorCode::kInvalidArgument,
                  "客户端不允许主动发送该 WebSocket 事件类型");
        return;
    }

    sendError(connection,
              envelope.requestId,
              protocol::error::ErrorCode::kInvalidArgument,
              "暂不支持该 WebSocket 顶层事件类型");
}

void ChatWsController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &connection)
{
    const auto context = wsSessionService_.connectionContext(connection);
    const std::string peerAddress =
        connection != nullptr ? connection->peerAddr().toIpPort() : "";
    const bool wasConnected =
        connection != nullptr ? connection->connected() : false;
    const bool wasDisconnected =
        connection != nullptr ? connection->disconnected() : false;

    wsSessionService_.handleConnectionClosed(connection);
    if (context.has_value())
    {
        CHATSERVER_LOG_INFO(kWsLogTag)
            << "实时通道连接已关闭，来源=" << peerAddress
            << " user_id=" << context->userId
            << " device_session_id=" << context->deviceSessionId
            << " device_id=" << context->deviceId
            << " connected=" << wasConnected
            << " disconnected=" << wasDisconnected;
        return;
    }

    CHATSERVER_LOG_INFO(kWsLogTag)
        << "实时通道匿名连接已关闭，来源=" << peerAddress
        << " connected=" << wasConnected
        << " disconnected=" << wasDisconnected;
}

}  // namespace chatserver::transport::ws
