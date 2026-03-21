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

struct AttachmentMessageRouteConfig
{
    std::string ackRoute;
    std::string expectedMediaKind;
    std::string messageType;
    std::string startLogLabel;
    std::string memberQueryLogLabel;
    std::string prepareFailureLogLabel;
    std::string createFailureLogLabel;
    std::string queryFailureLogLabel;
};

/**
 * @brief 统一规整附件型消息的可选字段。
 * @param clientMessageId 待规整的 client_message_id。
 * @param caption 待规整的 caption。
 * @param routeName 当前 ack route，用于失败时返回给客户端。
 * @param requestId 当前 ws.send 请求 ID。
 * @param connection 当前 WebSocket 连接。
 * @return true 表示字段合法；false 表示已向客户端回 ack 失败。
 */
bool normalizeAttachmentOptionalFields(
    std::optional<std::string> &clientMessageId,
    std::optional<std::string> &caption,
    const std::string &routeName,
    const std::string &requestId,
    const drogon::WebSocketConnectionPtr &connection,
    const RealtimePushService &realtimePushService)
{
    if (clientMessageId.has_value())
    {
        *clientMessageId = trimCopy(*clientMessageId);
        if (clientMessageId->empty())
        {
            clientMessageId.reset();
        }
    }

    if (caption.has_value())
    {
        *caption = trimCopy(*caption);
        if (caption->empty())
        {
            caption.reset();
        }
        else if (caption->size() > 4000)
        {
            realtimePushService.pushAckToConnection(
                connection,
                requestId,
                routeName,
                false,
                protocol::error::ErrorCode::kInvalidArgument,
                "caption is too long");
            return false;
        }
    }

    return true;
}

/**
 * @brief 统一处理“基于临时附件确认”的消息发送链路。
 *
 * 这条共享路径服务于 `message.send_image` 和 `message.send_file`：
 * 1. 校验请求结构和会话成员关系；
 * 2. 把临时附件转成正式附件；
 * 3. 调用不同的 repository 写入最终 message；
 * 4. 成功后统一回 ack + 广播 message.created；
 * 5. 失败时统一做正式附件回滚或临时上传保留。
 */
template <typename CreateMessageFn>
void handleAttachmentBackedMessage(
    Json::Value data,
    std::string requestId,
    WsConnectionContext context,
    const drogon::WebSocketConnectionPtr &connection,
    const AttachmentMessageRouteConfig &config,
    protocol::dto::conversation::SendFileMessageRequest request,
    const repository::ConversationRepository &conversationRepository,
    const FileService &fileService,
    const RealtimePushService &realtimePushService,
    CreateMessageFn &&createMessage)
{
    if (connection == nullptr)
    {
        return;
    }

    if (!data.isObject())
    {
        realtimePushService.pushAckToConnection(
            connection,
            requestId,
            config.ackRoute,
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "ws.send payload.data must be an object");
        return;
    }

    if (!data.isMember("conversation_id") ||
        !data["conversation_id"].isString())
    {
        realtimePushService.pushAckToConnection(
            connection,
            requestId,
            config.ackRoute,
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "field 'conversation_id' must be a string");
        return;
    }

    std::string conversationId = trimCopy(data["conversation_id"].asString());
    if (conversationId.empty())
    {
        realtimePushService.pushAckToConnection(
            connection,
            requestId,
            config.ackRoute,
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "conversation_id is required");
        return;
    }

    request.attachmentUploadKey = trimCopy(request.attachmentUploadKey);
    if (request.attachmentUploadKey.empty())
    {
        realtimePushService.pushAckToConnection(
            connection,
            requestId,
            config.ackRoute,
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            "attachment_upload_key is required");
        return;
    }

    if (!normalizeAttachmentOptionalFields(request.clientMessageId,
                                           request.caption,
                                           config.ackRoute,
                                           requestId,
                                           connection,
                                           realtimePushService))
    {
        return;
    }

    auto sharedConnection =
        std::make_shared<drogon::WebSocketConnectionPtr>(connection);
    auto sharedRequestId = std::make_shared<std::string>(std::move(requestId));
    auto sharedContext = std::make_shared<WsConnectionContext>(std::move(context));

    CHATSERVER_LOG_INFO(kWsMessageLogTag)
        << config.startLogLabel << "，conversation_id=" << conversationId
        << " sender_id=" << sharedContext->userId
        << " upload_key=" << request.attachmentUploadKey;

    const std::string requestConversationId = conversationId;
    conversationRepository.listConversationMemberUserIds(
        requestConversationId,
        [&conversationRepository,
         &fileService,
         &realtimePushService,
         config,
         createMessage = std::forward<CreateMessageFn>(createMessage),
         conversationId = requestConversationId,
         request = std::move(request),
         sharedConnection,
         sharedRequestId,
         sharedContext](
            std::vector<std::string> userIds) mutable {
            CHATSERVER_LOG_INFO(kWsMessageLogTag)
                << config.memberQueryLogLabel
                << "，conversation_id=" << conversationId
                << " sender_id=" << sharedContext->userId
                << " member_count=" << userIds.size();
            const auto memberIt = std::find(userIds.begin(),
                                            userIds.end(),
                                            sharedContext->userId);
            if (memberIt == userIds.end())
            {
                realtimePushService.pushAckToConnection(
                    *sharedConnection,
                    *sharedRequestId,
                    config.ackRoute,
                    false,
                    protocol::error::ErrorCode::kNotFound,
                    "conversation not found");
                return;
            }

            // 这里和图片消息一样，先把真正会跨异步层使用的字段抽成稳定副本。
            // 这样可以避免“边读 attachmentUploadKey，边 move 整个 request”带来的
            // 求值顺序问题，也让 image/file 两条链路保持同一套安全写法。
            const std::string attachmentUploadKey = request.attachmentUploadKey;
            const std::optional<std::string> clientMessageId =
                request.clientMessageId;
            const std::optional<std::string> caption = request.caption;

            fileService.prepareAttachmentForMessage(
                attachmentUploadKey,
                sharedContext->userId,
                std::optional<std::string>(config.expectedMediaKind),
                [&fileService,
                 &realtimePushService,
                 config,
                 createMessage = std::move(createMessage),
                 userIds = std::move(userIds),
                 conversationId,
                 clientMessageId,
                 caption,
                 sharedConnection,
                 sharedRequestId](
                    PreparedAttachmentResult prepared) mutable {
                    // prepared 后面会同时用于三处：
                    // 1. 传给 createMessage 写最终消息；
                    // 2. 成功后清理临时上传；
                    // 3. 失败后回滚正式附件。
                    //
                    // 这里如果直接在一次函数调用里对同一个 prepared 连续做
                    // 多次 std::move，会再次踩到“参数求值顺序导致对象被提前掏空”
                    // 的问题，最终让 sender_id / attachment_id / file_name 等字段
                    // 偶发变成空字符串。
                    //
                    // 因此这里显式拆成三份稳定副本：
                    // - preparedForCreate：专门交给 repository 写消息
                    // - preparedForSuccess：消息成功后用于删除临时上传
                    // - preparedForFailure：消息失败后用于回滚正式附件
                    PreparedAttachmentResult preparedForCreate = prepared;
                    PreparedAttachmentResult preparedForSuccess = prepared;
                    PreparedAttachmentResult preparedForFailure =
                        std::move(prepared);

                    createMessage(conversationId,
                                  std::move(preparedForCreate),
                                  clientMessageId,
                                  caption,
                                  [config,
                                   &fileService,
                                   &realtimePushService,
                                   prepared =
                                       std::move(preparedForSuccess),
                                   userIds = std::move(userIds),
                                   sharedConnection,
                                   sharedRequestId](
                                      repository::ConversationMessageRecord
                                          record) mutable {
                                      auto view = toMessageView(record);

                                      realtimePushService.pushAckToConnection(
                                          *sharedConnection,
                                          *sharedRequestId,
                                          config.ackRoute,
                                          true,
                                          protocol::error::ErrorCode::kOk,
                                          "ok",
                                          buildAckData(view));

                                      Json::Value newData =
                                          protocol::dto::conversation::toJson(
                                              view);
                                      realtimePushService.pushNewToUsers(
                                          userIds,
                                          "message.created",
                                          std::move(newData));

                                      fileService.removeTemporaryUploadQuietly(
                                          prepared.attachmentUploadKey);
                                  },
                                  [config,
                                   &fileService,
                                   &realtimePushService,
                                   prepared =
                                       std::move(preparedForFailure),
                                   sharedConnection,
                                   sharedRequestId](
                                      std::string message) mutable {
                                      CHATSERVER_LOG_ERROR(kWsMessageLogTag)
                                          << config.createFailureLogLabel
                                          << "：" << message;
                                      fileService
                                          .rollbackPreparedAttachmentQuietly(
                                              prepared.attachment);
                                      realtimePushService.pushAckToConnection(
                                          *sharedConnection,
                                          *sharedRequestId,
                                          config.ackRoute,
                                          false,
                                          protocol::error::ErrorCode::
                                              kInternalError,
                                          "failed to create message");
                                  });
                },
                [config, &realtimePushService, sharedConnection, sharedRequestId](
                    ServiceError error) mutable {
                    CHATSERVER_LOG_WARN(kWsMessageLogTag)
                        << config.prepareFailureLogLabel << "，code="
                        << static_cast<int>(error.code) << " message="
                        << error.message;
                    realtimePushService.pushAckToConnection(
                        *sharedConnection,
                        *sharedRequestId,
                        config.ackRoute,
                        false,
                        error.code,
                        error.message);
                });
        },
        [config, &realtimePushService, sharedConnection, sharedRequestId, conversationId](
            std::string message) mutable {
            CHATSERVER_LOG_ERROR(kWsMessageLogTag)
                << config.queryFailureLogLabel
                << "，conversation_id=" << conversationId
                << " 原因：" << message;
            realtimePushService.pushAckToConnection(
                *sharedConnection,
                *sharedRequestId,
                config.ackRoute,
                false,
                protocol::error::ErrorCode::kInternalError,
                "failed to query conversation");
        });
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

    protocol::dto::conversation::SendFileMessageRequest attachmentRequest;
    attachmentRequest.clientMessageId = request.clientMessageId;
    attachmentRequest.attachmentUploadKey = request.attachmentUploadKey;
    attachmentRequest.caption = request.caption;

    handleAttachmentBackedMessage(
        std::move(data),
        std::move(requestId),
        std::move(context),
        connection,
        AttachmentMessageRouteConfig{
            "message.send_image",
            "image",
            "image",
            "开始处理图片消息",
            "图片消息成员查询完成",
            "准备正式图片附件失败",
            "写入图片消息失败",
            "发送图片消息前查询会话失败",
        },
        std::move(attachmentRequest),
        conversationRepository_,
        fileService_,
        realtimePushService_,
        [this](const std::string &conversationId,
               PreparedAttachmentResult prepared,
               const std::optional<std::string> &clientMessageId,
               const std::optional<std::string> &caption,
               auto &&onSuccess,
               auto &&onFailure) {
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

            conversationRepository_.createImageMessage(std::move(params),
                                                       std::forward<decltype(onSuccess)>(onSuccess),
                                                       std::forward<decltype(onFailure)>(onFailure));
        });
}

void WsMessageService::handleSendFileMessage(
    Json::Value data,
    std::string requestId,
    WsConnectionContext context,
    const drogon::WebSocketConnectionPtr &connection) const
{
    // 文件消息与图片消息共用同一套“先传 HTTP 临时附件，再走 WS 确认”的大骨架。
    // 唯一的关键区别是：
    // 1. 期待的 media_kind 是 file；
    // 2. 最终写入 messages 时 message_type = file；
    // 3. content 里不包含 width / height，而是只保留文件摘要字段。
    protocol::dto::conversation::SendFileMessageRequest request;
    std::string parseError;
    if (!protocol::dto::conversation::parseSendFileMessageRequest(
            data, request, parseError))
    {
        realtimePushService_.pushAckToConnection(
            connection,
            requestId,
            "message.send_file",
            false,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError);
        return;
    }

    handleAttachmentBackedMessage(
        std::move(data),
        std::move(requestId),
        std::move(context),
        connection,
        AttachmentMessageRouteConfig{
            "message.send_file",
            "file",
            "file",
            "开始处理文件消息",
            "文件消息成员查询完成",
            "准备正式文件附件失败",
            "写入文件消息失败",
            "发送文件消息前查询会话失败",
        },
        std::move(request),
        conversationRepository_,
        fileService_,
        realtimePushService_,
        [this](const std::string &conversationId,
               PreparedAttachmentResult prepared,
               const std::optional<std::string> &clientMessageId,
               const std::optional<std::string> &caption,
               auto &&onSuccess,
               auto &&onFailure) {
            infra::id::IdGenerator idGenerator;
            repository::CreateFileMessageParams params;
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

            conversationRepository_.createFileMessage(std::move(params),
                                                      std::forward<decltype(onSuccess)>(onSuccess),
                                                      std::forward<decltype(onFailure)>(onFailure));
        });
}

}  // namespace chatserver::service
