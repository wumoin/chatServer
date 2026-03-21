#include "transport/http/conversation_controller.h"

#include "protocol/dto/conversation/conversation_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/drogon.h>

#include <cctype>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// ConversationController 是会话域 HTTP 入口。
// 当前它承接：
// - 创建 / 获取私聊
// - 获取会话列表
// - 获取历史消息
// - 通过 HTTP 发送文本消息
//
// controller 本身保持很薄，只做协议转换和错误映射。
namespace chatserver::transport::http {
namespace {

std::string resolveRequestId(const drogon::HttpRequestPtr &request)
{
    auto requestId = request->getHeader("X-Request-Id");
    if (requestId.empty())
    {
        requestId = request->getHeader("x-request-id");
    }
    if (!requestId.empty())
    {
        return requestId;
    }
    return drogon::utils::getUuid(true);
}

std::string trimCopy(const std::string_view input)
{
    std::size_t begin = 0;
    std::size_t end = input.size();

    while (begin < end &&
           std::isspace(static_cast<unsigned char>(input[begin])) != 0)
    {
        ++begin;
    }

    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
    {
        --end;
    }

    return std::string(input.substr(begin, end - begin));
}

std::optional<std::string> resolveBearerAccessToken(
    const drogon::HttpRequestPtr &request)
{
    // 会话域所有 HTTP 接口都要求 Bearer access token。
    // controller 这里只筛掉明显不合法的 header，真正 token 语义仍交给 service。
    auto authorization = request->getHeader("Authorization");
    if (authorization.empty())
    {
        authorization = request->getHeader("authorization");
    }

    authorization = trimCopy(authorization);
    if (authorization.size() <= 7)
    {
        return std::nullopt;
    }

    const std::string_view headerView(authorization);
    const std::string_view scheme = headerView.substr(0, 6);
    if (!(scheme == "Bearer" || scheme == "bearer" || scheme == "BEARER") ||
        headerView[6] != ' ')
    {
        return std::nullopt;
    }

    const std::string token = trimCopy(headerView.substr(7));
    if (token.empty())
    {
        return std::nullopt;
    }

    return token;
}

bool parsePositiveInt64(const std::string &text, std::int64_t &value)
{
    if (text.empty())
    {
        return false;
    }

    std::int64_t parsedValue = 0;
    for (const char ch : text)
    {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }

        if (parsedValue >
            (std::numeric_limits<std::int64_t>::max() - (ch - '0')) / 10)
        {
            return false;
        }

        parsedValue = parsedValue * 10 + (ch - '0');
    }

    value = parsedValue;
    return true;
}

drogon::HttpResponsePtr makeResponse(drogon::HttpStatusCode statusCode,
                                     const std::string &requestId,
                                     const protocol::error::ErrorCode code,
                                     const std::string &message,
                                     Json::Value data = Json::Value(
                                         Json::objectValue))
{
    Json::Value body(Json::objectValue);
    body["code"] = static_cast<int>(code);
    body["message"] = message;
    body["request_id"] = requestId;
    body["data"] = std::move(data);

    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(statusCode);
    return response;
}

drogon::HttpStatusCode mapServiceErrorToStatus(
    const service::ServiceError &error)
{
    switch (error.code)
    {
    case protocol::error::ErrorCode::kInvalidArgument:
        return drogon::k400BadRequest;
    case protocol::error::ErrorCode::kInvalidAccessToken:
        return drogon::k401Unauthorized;
    case protocol::error::ErrorCode::kForbidden:
        return drogon::k403Forbidden;
    case protocol::error::ErrorCode::kNotFound:
        return drogon::k404NotFound;
    case protocol::error::ErrorCode::kOk:
        return drogon::k200OK;
    case protocol::error::ErrorCode::kInternalError:
    default:
        return drogon::k500InternalServerError;
    }
}

}  // namespace

void ConversationController::createPrivateConversation(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    const std::string requestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            requestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    const auto json = request->getJsonObject();
    if (json == nullptr)
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidJson,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidJson)));
        return;
    }

    protocol::dto::conversation::CreatePrivateConversationRequest createRequest;
    std::string parseError;
    if (!protocol::dto::conversation::parseCreatePrivateConversationRequest(
            *json, createRequest, parseError))
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError));
        return;
    }

    // “创建私聊”语义其实是 create-or-find：
    // service 可能新建，也可能复用既有一对一会话，controller 不关心具体来源。
    conversationService_.createOrFindPrivateConversation(
        std::move(createRequest),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::conversation::ConversationListItemView item) mutable {
            Json::Value data(Json::objectValue);
            data["conversation"] = protocol::dto::conversation::toJson(item);
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void ConversationController::listConversations(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    const std::string requestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            requestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    // 这里返回的是会话列表快照；增量实时更新属于 WS 推送链路。
    conversationService_.listConversations(
        *accessToken,
        [sharedCallback, requestId](
            std::vector<protocol::dto::conversation::ConversationListItemView> items) mutable {
            Json::Value data(Json::objectValue);
            data["conversations"] = protocol::dto::conversation::toJson(items);
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void ConversationController::getConversationDetail(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string conversationId) const
{
    const std::string requestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            requestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    conversationService_.getConversationDetail(
        std::move(conversationId),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::conversation::ConversationDetailView item) mutable {
            Json::Value data(Json::objectValue);
            data["conversation"] = protocol::dto::conversation::toJson(item);
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void ConversationController::listMessages(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string conversationId) const
{
    const std::string requestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            requestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    protocol::dto::conversation::ListConversationMessagesRequest query;
    query.limit = 50;

    // 历史消息接口走 before_seq / after_seq 分页。
    // controller 只把 query string 解析成标准请求对象，不决定翻页策略。
    const auto limitText = request->getOptionalParameter<std::string>("limit");
    if (limitText.has_value())
    {
        std::int64_t parsedLimit = 0;
        if (!parsePositiveInt64(*limitText, parsedLimit))
        {
            (*sharedCallback)(makeResponse(
                drogon::k400BadRequest,
                requestId,
                protocol::error::ErrorCode::kInvalidArgument,
                "limit must be a positive integer"));
            return;
        }
        query.limit = static_cast<std::size_t>(parsedLimit);
    }

    const auto beforeSeqText =
        request->getOptionalParameter<std::string>("before_seq");
    if (beforeSeqText.has_value())
    {
        std::int64_t parsedBeforeSeq = 0;
        if (!parsePositiveInt64(*beforeSeqText, parsedBeforeSeq))
        {
            (*sharedCallback)(makeResponse(
                drogon::k400BadRequest,
                requestId,
                protocol::error::ErrorCode::kInvalidArgument,
                "before_seq must be a positive integer"));
            return;
        }
        query.beforeSeq = parsedBeforeSeq;
    }

    const auto afterSeqText =
        request->getOptionalParameter<std::string>("after_seq");
    if (afterSeqText.has_value())
    {
        std::int64_t parsedAfterSeq = 0;
        if (!parsePositiveInt64(*afterSeqText, parsedAfterSeq))
        {
            (*sharedCallback)(makeResponse(
                drogon::k400BadRequest,
                requestId,
                protocol::error::ErrorCode::kInvalidArgument,
                "after_seq must be a positive integer"));
            return;
        }
        query.afterSeq = parsedAfterSeq;
    }

    conversationService_.listMessages(
        std::move(conversationId),
        std::move(query),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::conversation::ListConversationMessagesResult result) mutable {
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                protocol::dto::conversation::toJson(result)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void ConversationController::sendTextMessage(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string conversationId) const
{
    const std::string requestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            requestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    const auto json = request->getJsonObject();
    if (json == nullptr)
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidJson,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidJson)));
        return;
    }

    protocol::dto::conversation::SendTextMessageRequest sendRequest;
    std::string parseError;
    if (!protocol::dto::conversation::parseSendTextMessageRequest(
            *json, sendRequest, parseError))
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError));
        return;
    }

    // 这是 HTTP 命令式发送入口。
    // controller 只返回“本次消息已写入”的结果，不在这里补做 ws.new 广播。
    conversationService_.sendTextMessage(
        std::move(conversationId),
        std::move(sendRequest),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::conversation::ConversationMessageView item) mutable {
            Json::Value data(Json::objectValue);
            data["message"] = protocol::dto::conversation::toJson(item);
            (*sharedCallback)(makeResponse(
                drogon::k201Created,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

}  // namespace chatserver::transport::http
