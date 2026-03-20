#include "service/ws_message_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "protocol/dto/conversation/conversation_dto.h"

#include <json/value.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chatserver::service {
namespace {

constexpr auto kWsMessageLogTag = "ws.message";

std::string trimCopy(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

protocol::dto::conversation::ConversationMessageView toMessageView(
    const repository::ConversationMessageRecord &item)
{
    protocol::dto::conversation::ConversationMessageView view;
    view.messageId = item.messageId;
    view.conversationId = item.conversationId;
    view.seq = item.seq;
    view.senderId = item.senderId;
    view.clientMessageId = item.clientMessageId;
    view.messageType = item.messageType;
    view.content = item.contentJson;
    view.createdAtMs = item.createdAtMs;
    return view;
}

Json::Value buildAckData(
    const protocol::dto::conversation::ConversationMessageView &message)
{
    Json::Value data(Json::objectValue);
    data["conversation_id"] = message.conversationId;
    data["message_id"] = message.messageId;
    data["seq"] = Json::Int64(message.seq);
    data["sender_id"] = message.senderId;
    data["message_type"] = message.messageType;
    data["created_at_ms"] = Json::Int64(message.createdAtMs);
    if (message.clientMessageId.has_value())
    {
        data["client_message_id"] = *message.clientMessageId;
    }
    return data;
}

}  // namespace

void WsMessageService::handleSendTextMessage(
    Json::Value data,
    std::string requestId,
    WsConnectionContext context,
    const drogon::WebSocketConnectionPtr &connection) const
{
    // 文本消息的 WS 处理链路：
    // 1. 校验 payload 与会话参数
    // 2. 校验当前连接用户是否属于该会话
    // 3. 写入 messages
    // 4. 给发送方回 ws.ack
    // 5. 给会话在线成员推 ws.new(message.created)
    if (connection == nullptr)
    {
        return;
    }

    if (!data.isObject())
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_text",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "ws.send payload.data must be an object");
        return;
    }

    if (!data.isMember("conversation_id") ||
        !data["conversation_id"].isString())
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_text",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "field 'conversation_id' must be a string");
        return;
    }

    std::string conversationId = trimCopy(data["conversation_id"].asString());
    if (conversationId.empty())
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_text",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "conversation_id is required");
        return;
    }

    protocol::dto::conversation::SendTextMessageRequest request;
    std::string parseError;
    if (!protocol::dto::conversation::parseSendTextMessageRequest(
            data, request, parseError))
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_text",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError);
        return;
    }

    request.text = trimCopy(request.text);
    if (request.text.empty())
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_text",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "text must not be empty");
        return;
    }

    if (request.text.size() > 4000)
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_text",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "text is too long");
        return;
    }

    if (request.clientMessageId.has_value())
    {
        *request.clientMessageId = trimCopy(*request.clientMessageId);
        if (request.clientMessageId->empty())
        {
            request.clientMessageId.reset();
        }
    }

    auto sharedConnection =
        std::make_shared<drogon::WebSocketConnectionPtr>(connection);
    auto sharedRequestId = std::make_shared<std::string>(std::move(requestId));
    auto sharedContext = std::make_shared<WsConnectionContext>(std::move(context));

    CHATSERVER_LOG_INFO(kWsMessageLogTag)
        << "开始处理文本消息，conversation_id=" << conversationId
        << " sender_id=" << sharedContext->userId;

    const std::string requestConversationId = conversationId;
    conversationRepository_.listConversationMemberUserIds(
        requestConversationId,
        [this,
         conversationId = requestConversationId,
         request = std::move(request),
         sharedConnection,
         sharedRequestId,
         sharedContext](
            std::vector<std::string> userIds) mutable {
            CHATSERVER_LOG_INFO(kWsMessageLogTag)
                << "文本消息成员查询完成，conversation_id=" << conversationId
                << " sender_id=" << sharedContext->userId
                << " member_count=" << userIds.size();
            const auto memberIt = std::find(userIds.begin(),
                                            userIds.end(),
                                            sharedContext->userId);
            if (memberIt == userIds.end())
            {
                // 从外部行为上统一表现为“conversation not found”，
                // 避免把会话存在性和成员权限差异暴露给客户端。
                realtimePushService_.pushAckToConnection(
                    *sharedConnection,
                    *sharedRequestId,
                    "message.send_text",
                    false,
                    protocol::error::ErrorCode::kNotFound,
                    "conversation not found");
                return;
            }

            infra::id::IdGenerator idGenerator;
            repository::CreateTextMessageParams params;
            params.messageId = idGenerator.nextMessageId();
            params.conversationId = conversationId;
            params.senderId = sharedContext->userId;
            params.clientMessageId = request.clientMessageId;
            params.text = request.text;

            conversationRepository_.createTextMessage(
                std::move(params),
                [this,
                 userIds = std::move(userIds),
                 conversationId,
                 sharedConnection,
                 sharedRequestId](
                    repository::ConversationMessageRecord record) mutable {
                    auto view = toMessageView(record);

                    realtimePushService_.pushAckToConnection(
                        *sharedConnection,
                        *sharedRequestId,
                        "message.send_text",
                        true,
                        protocol::error::ErrorCode::kOk,
                        "ok",
                        buildAckData(view));

                    Json::Value newData =
                        protocol::dto::conversation::toJson(view);

                    // 所有在线会话成员（包括发送者自己）都会收到同一条正式消息事件，
                    // 这样每个客户端都能用统一的 message.created 路径更新本地状态。
                    realtimePushService_.pushNewToUsers(
                        userIds,
                        "message.created",
                        std::move(newData));
                },
                [this, sharedConnection, sharedRequestId](
                    std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kWsMessageLogTag)
                        << "写入文本消息失败：" << message;
                    realtimePushService_.pushAckToConnection(
                        *sharedConnection,
                        *sharedRequestId,
                        "message.send_text",
                        false,
                        protocol::error::ErrorCode::kInternalError,
                        "failed to create message");
                });
        },
        [this, sharedConnection, sharedRequestId, conversationId](
            std::string message) mutable {
            CHATSERVER_LOG_ERROR(kWsMessageLogTag)
                << "发送文本消息前查询会话失败，conversation_id=" << conversationId
                << " 原因：" << message;
            realtimePushService_.pushAckToConnection(
                *sharedConnection,
                *sharedRequestId,
                "message.send_text",
                false,
                protocol::error::ErrorCode::kInternalError,
                "failed to query conversation");
        });
}

}  // namespace chatserver::service
