#include "service/realtime_push_service.h"

#include "infra/log/app_logger.h"
#include "infra/ws/connection_registry.h"
#include "protocol/dto/ws/ws_business_dto.h"
#include "protocol/dto/ws/ws_envelope_dto.h"

#include <chrono>
#include <unordered_set>

namespace chatserver::service {
namespace {

constexpr auto kRealtimePushLogTag = "ws.push";

std::int64_t nowEpochMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

bool canDeliver(const drogon::WebSocketConnectionPtr &connection)
{
    return connection != nullptr && connection->connected();
}

void sendEnvelope(const drogon::WebSocketConnectionPtr &connection,
                  std::string type,
                  std::string requestId,
                  Json::Value payload)
{
    if (!canDeliver(connection))
    {
        return;
    }

    protocol::dto::ws::WsEnvelope envelope;
    envelope.version = 1;
    envelope.type = std::move(type);
    envelope.requestId = std::move(requestId);
    envelope.tsMs = nowEpochMs();
    envelope.payload = std::move(payload);
    connection->sendJson(protocol::dto::ws::toJson(envelope));
}

}  // namespace

void RealtimePushService::pushAckToConnection(
    const drogon::WebSocketConnectionPtr &connection,
    const std::string &requestId,
    const std::string &route,
    const bool ok,
    const protocol::error::ErrorCode code,
    std::string message,
    Json::Value data) const
{
    if (!canDeliver(connection))
    {
        return;
    }

    protocol::dto::ws::WsAckPayload payload;
    payload.route = route;
    payload.ok = ok;
    payload.code = static_cast<int>(code);
    payload.message = std::move(message);
    payload.data =
        data.isObject() ? std::move(data) : Json::Value(Json::objectValue);

    sendEnvelope(connection,
                 "ws.ack",
                 requestId,
                 protocol::dto::ws::toJson(payload));
}

void RealtimePushService::pushAckToDeviceSession(
    const std::string &deviceSessionId,
    const std::string &requestId,
    const std::string &route,
    const bool ok,
    const protocol::error::ErrorCode code,
    std::string message,
    Json::Value data) const
{
    auto connection =
        infra::ws::ConnectionRegistry::findConnectionByDeviceSessionId(
            deviceSessionId);
    if (connection == nullptr)
    {
        CHATSERVER_LOG_DEBUG(kRealtimePushLogTag)
            << "跳过 ws.ack 推送，目标设备会话当前不在线，device_session_id="
            << deviceSessionId << " route=" << route;
        return;
    }

    pushAckToConnection(connection,
                        requestId,
                        route,
                        ok,
                        code,
                        std::move(message),
                        std::move(data));
}

void RealtimePushService::pushNewToConnection(
    const drogon::WebSocketConnectionPtr &connection,
    const std::string &route,
    Json::Value data,
    const std::string &requestId) const
{
    if (!canDeliver(connection))
    {
        return;
    }

    protocol::dto::ws::WsNewPayload payload;
    payload.route = route;
    payload.data =
        data.isObject() ? std::move(data) : Json::Value(Json::objectValue);

    sendEnvelope(connection,
                 "ws.new",
                 requestId,
                 protocol::dto::ws::toJson(payload));
}

void RealtimePushService::pushNewToDeviceSession(
    const std::string &deviceSessionId,
    const std::string &route,
    Json::Value data,
    const std::string &requestId) const
{
    auto connection =
        infra::ws::ConnectionRegistry::findConnectionByDeviceSessionId(
            deviceSessionId);
    if (connection == nullptr)
    {
        CHATSERVER_LOG_DEBUG(kRealtimePushLogTag)
            << "跳过 ws.new 推送，目标设备会话当前不在线，device_session_id="
            << deviceSessionId << " route=" << route;
        return;
    }

    pushNewToConnection(connection, route, std::move(data), requestId);
}

void RealtimePushService::pushNewToUser(const std::string &userId,
                                        const std::string &route,
                                        Json::Value data,
                                        const std::string &requestId) const
{
    auto connections = infra::ws::ConnectionRegistry::findConnectionsByUserId(
        userId);
    if (connections.empty())
    {
        CHATSERVER_LOG_DEBUG(kRealtimePushLogTag)
            << "跳过 ws.new 推送，目标用户当前没有在线连接，user_id="
            << userId << " route=" << route;
        return;
    }

    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        Json::Value payloadData =
            index == 0 ? data : data;
        pushNewToConnection(connections[index], route, std::move(payloadData), requestId);
    }
}

void RealtimePushService::pushNewToUsers(const std::vector<std::string> &userIds,
                                         const std::string &route,
                                         Json::Value data,
                                         const std::string &requestId) const
{
    std::unordered_set<std::string> uniqueUserIds(userIds.begin(), userIds.end());
    std::unordered_set<const void *> pushedConnections;

    for (const auto &userId : uniqueUserIds)
    {
        auto connections =
            infra::ws::ConnectionRegistry::findConnectionsByUserId(userId);
        for (const auto &connection : connections)
        {
            if (!canDeliver(connection))
            {
                continue;
            }

            if (!pushedConnections.insert(connection.get()).second)
            {
                continue;
            }

            Json::Value payloadData = data;
            pushNewToConnection(connection, route, std::move(payloadData), requestId);
        }
    }
}

}  // namespace chatserver::service
