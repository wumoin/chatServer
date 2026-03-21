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
    // 推送层采用“尽力而为”策略。目标连接不在线时直接跳过，
    // 不能让实时推送反过来影响主业务事务是否成功。
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

    // ws.ack 表示“某次请求已经被服务端处理完成”，因此必须保留 request_id，
    // 客户端才能把这个确认和对应的 ws.send 对上。
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
    // 发送确认通常只需要回给发起请求的那台设备，而不是广播给同一用户的所有在线端。
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

    // ws.new 表示服务端主动推送“有新事件发生”，比如 message.created、
    // conversation.created、friend.request.new 都会复用这一层发送。
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
    // device_session 维度推送适合“只回给发起端这一台设备”的事件。
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
    // 同一个用户可能在多个设备上在线，因此这里需要向该用户的所有在线连接广播。
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
        // Json::Value 不能被多个发送分支共享 move 后对象，
        // 因此除第一份之外都复制一份 payload。
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
    // 一个业务事件可能传入重复 user_id，也可能不同 user_id 最终映射到同一条连接。
    // 这里分别对 user 和 connection 做去重，避免同一帧被重复发送。
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
                // 同一条物理连接可能同时被多个 user_id 查询路径命中，按连接再去重一次。
                continue;
            }

            Json::Value payloadData = data;
            pushNewToConnection(connection, route, std::move(payloadData), requestId);
        }
    }
}

}  // namespace chatserver::service
