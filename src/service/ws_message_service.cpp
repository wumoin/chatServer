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

std::string buildAttachmentDownloadUrl(const std::string &attachmentId)
{
    return "/api/v1/files/" + attachmentId;
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
    //
    // 和 HTTP send-text 不同，WS 路径本身承担了“确认 + fan-out”这两个实时职责，
    // 因此当前它才是完整的在线文本消息链路。
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

void WsMessageService::handleSendImageMessage(
    Json::Value data,
    std::string requestId,
    WsConnectionContext context,
    const drogon::WebSocketConnectionPtr &connection) const
{
    // 图片消息链路在文本消息的基础上多了一层“临时附件 -> 正式附件”的确认：
    // 1. 校验 payload / conversation_id / attachment_upload_key
    // 2. 校验当前用户是否属于目标会话
    // 3. 把临时附件准备成正式附件
    // 4. 写入 image 消息
    // 5. 给发送方回 ws.ack(message.send_image)
    // 6. 给在线成员推 ws.new(message.created)
    if (connection == nullptr)
    {
        return;
    }

    if (!data.isObject())
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_image",
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
            "message.send_image",
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
            "message.send_image",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "conversation_id is required");
        return;
    }

    protocol::dto::conversation::SendImageMessageRequest request;
    std::string parseError;
    if (!protocol::dto::conversation::parseSendImageMessageRequest(
            data, request, parseError))
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_image",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError);
        return;
    }

    request.attachmentUploadKey = trimCopy(request.attachmentUploadKey);
    if (request.attachmentUploadKey.empty())
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_image",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "attachment_upload_key is required");
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

    if (request.caption.has_value())
    {
        *request.caption = trimCopy(*request.caption);
        if (request.caption->empty())
        {
            request.caption.reset();
        }
        else if (request.caption->size() > 4000)
        {
            realtimePushService_.pushAckToConnection(
                connection,
                requestId,
                "message.send_image",
                false,
                protocol::error::ErrorCode::kInvalidArgument,
                "caption is too long");
            return;
        }
    }

    auto sharedConnection =
        std::make_shared<drogon::WebSocketConnectionPtr>(connection);
    auto sharedRequestId = std::make_shared<std::string>(std::move(requestId));
    auto sharedContext = std::make_shared<WsConnectionContext>(std::move(context));

    CHATSERVER_LOG_INFO(kWsMessageLogTag)
        << "开始处理图片消息，conversation_id=" << conversationId
        << " sender_id=" << sharedContext->userId
        << " upload_key=" << request.attachmentUploadKey;

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
                << "图片消息成员查询完成，conversation_id=" << conversationId
                << " sender_id=" << sharedContext->userId
                << " member_count=" << userIds.size();
            const auto memberIt = std::find(userIds.begin(),
                                            userIds.end(),
                                            sharedContext->userId);
            if (memberIt == userIds.end())
            {
                realtimePushService_.pushAckToConnection(
                    *sharedConnection,
                    *sharedRequestId,
                    "message.send_image",
                    false,
                    protocol::error::ErrorCode::kNotFound,
                    "conversation not found");
                return;
            }

            // 这里先把后续真正需要的字段提取成稳定副本，再进入下一层异步。
            //
            // 原因是：如果在 `prepareAttachmentForMessage(...)` 这一行里，
            // 一边读取 `request.attachmentUploadKey`，一边又在 lambda capture
            // 中 `std::move(request)`，那么不同参数的求值顺序并没有我们能依赖
            // 的保证。这样会出现 upload key 还没来得及读出，`request` 就已经
            // 被 move 走的情况，最终导致文件服务看到空的
            // `attachment_upload_key`。
            //
            // 因此这里显式拆开为“先读字段，再把字段传入异步回调”，避免把
            // 整个 request 对象跨多层异步继续搬运，也让这条链路更容易阅读。
            const std::string attachmentUploadKey = request.attachmentUploadKey;
            const std::optional<std::string> clientMessageId =
                request.clientMessageId;
            const std::optional<std::string> caption = request.caption;

            fileService_.prepareAttachmentForMessage(
                attachmentUploadKey,
                sharedContext->userId,
                std::optional<std::string>("image"),
                [this,
                 userIds = std::move(userIds),
                 conversationId,
                 clientMessageId,
                 caption,
                 sharedConnection,
                 sharedRequestId](
                    PreparedAttachmentResult prepared) mutable {
                    infra::id::IdGenerator idGenerator;
                    repository::CreateImageMessageParams params;
                    params.messageId = idGenerator.nextMessageId();
                    params.conversationId = conversationId;
                    params.senderId = prepared.attachment.uploaderUserId;
                    params.clientMessageId = clientMessageId;
                    params.attachmentId = prepared.attachment.attachmentId;
                    params.fileName = prepared.attachment.originalFileName;
                    params.mimeType = prepared.attachment.mimeType;
                    params.sizeBytes = prepared.attachment.sizeBytes;
                    params.downloadUrl = buildAttachmentDownloadUrl(
                        prepared.attachment.attachmentId);
                    params.caption = caption;
                    params.imageWidth = prepared.attachment.imageWidth;
                    params.imageHeight = prepared.attachment.imageHeight;

                    conversationRepository_.createImageMessage(
                        std::move(params),
                        [this,
                         prepared = std::move(prepared),
                         userIds = std::move(userIds),
                         sharedConnection,
                         sharedRequestId](
                            repository::ConversationMessageRecord record) mutable {
                            auto view = toMessageView(record);

                            realtimePushService_.pushAckToConnection(
                                *sharedConnection,
                                *sharedRequestId,
                                "message.send_image",
                                true,
                                protocol::error::ErrorCode::kOk,
                                "ok",
                                buildAckData(view));

                            Json::Value newData =
                                protocol::dto::conversation::toJson(view);

                            realtimePushService_.pushNewToUsers(
                                userIds,
                                "message.created",
                                std::move(newData));

                            // 只有在正式消息已经落库并广播之后，临时上传才算真正完成使命。
                            fileService_.removeTemporaryUploadQuietly(
                                prepared.attachmentUploadKey);
                        },
                        [this,
                         prepared = std::move(prepared),
                         sharedConnection,
                         sharedRequestId](std::string message) mutable {
                            CHATSERVER_LOG_ERROR(kWsMessageLogTag)
                                << "写入图片消息失败：" << message;
                            fileService_.rollbackPreparedAttachmentQuietly(
                                prepared.attachment);
                            realtimePushService_.pushAckToConnection(
                                *sharedConnection,
                                *sharedRequestId,
                                "message.send_image",
                                false,
                                protocol::error::ErrorCode::kInternalError,
                                "failed to create message");
                        });
                },
                [this, sharedConnection, sharedRequestId](
                    ServiceError error) mutable {
                    CHATSERVER_LOG_WARN(kWsMessageLogTag)
                        << "准备正式图片附件失败，code="
                        << static_cast<int>(error.code) << " message="
                        << error.message;
                    realtimePushService_.pushAckToConnection(
                        *sharedConnection,
                        *sharedRequestId,
                        "message.send_image",
                        false,
                        error.code,
                        error.message);
                });
        },
        [this, sharedConnection, sharedRequestId, conversationId](
            std::string message) mutable {
            CHATSERVER_LOG_ERROR(kWsMessageLogTag)
                << "发送图片消息前查询会话失败，conversation_id=" << conversationId
                << " 原因：" << message;
            realtimePushService_.pushAckToConnection(
                *sharedConnection,
                *sharedRequestId,
                "message.send_image",
                false,
                protocol::error::ErrorCode::kInternalError,
                "failed to query conversation");
        });
}

}  // namespace chatserver::service
