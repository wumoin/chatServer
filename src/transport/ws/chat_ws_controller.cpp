#include "transport/ws/chat_ws_controller.h"

#include "infra/log/app_logger.h"
#include "protocol/dto/ws/ws_auth_dto.h"
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
        errorMessage = errors.empty() ? "invalid websocket json body" : errors;
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
        << "websocket connected from " << request->peerAddr().toIpPort()
        << ", waiting for ws.auth";
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

    if (type != drogon::WebSocketMessageType::Text)
    {
        sendError(connection,
                  "",
                  protocol::error::ErrorCode::kInvalidArgument,
                  "only text websocket messages are supported");
        connection->shutdown(drogon::CloseCode::kInvalidMessage,
                             "only text websocket messages are supported");
        return;
    }

    Json::Value json;
    std::string parseError;
    if (!parseJsonMessage(message, json, parseError))
    {
        CHATSERVER_LOG_WARN(kWsLogTag)
            << "websocket rejected invalid json message: " << parseError;
        sendError(connection,
                  "",
                  protocol::error::ErrorCode::kInvalidArgument,
                  "invalid websocket json body");
        connection->shutdown(drogon::CloseCode::kWrongMessageContent,
                             "invalid websocket json body");
        return;
    }

    protocol::dto::ws::WsEnvelope envelope;
    if (!protocol::dto::ws::parseWsEnvelope(json, envelope, parseError))
    {
        CHATSERVER_LOG_WARN(kWsLogTag)
            << "websocket rejected invalid envelope: " << parseError;
        sendError(connection,
                  "",
                  protocol::error::ErrorCode::kInvalidArgument,
                  parseError);
        connection->shutdown(drogon::CloseCode::kWrongMessageContent,
                             "invalid websocket envelope");
        return;
    }

    if (envelope.type == "ws.auth")
    {
        if (wsSessionService_.isAuthenticated(connection))
        {
            sendError(connection,
                      envelope.requestId,
                      protocol::error::ErrorCode::kInvalidArgument,
                      "websocket connection is already authenticated");
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
                                 "invalid ws.auth payload");
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
                    << "ws.auth accepted for device_session_id="
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
                  "websocket connection is not authenticated");
        connection->shutdown(drogon::CloseCode::kViolation,
                             "websocket connection is not authenticated");
        return;
    }

    if (envelope.type == "ping")
    {
        sendEnvelope(connection,
                     "pong",
                     envelope.requestId,
                     Json::Value(Json::objectValue));
        return;
    }

    sendError(connection,
              envelope.requestId,
              protocol::error::ErrorCode::kInvalidArgument,
              "unsupported websocket event type");
}

void ChatWsController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &connection)
{
    wsSessionService_.handleConnectionClosed(connection);
    CHATSERVER_LOG_INFO(kWsLogTag) << "websocket connection closed";
}

}  // namespace chatserver::transport::ws
