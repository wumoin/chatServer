#include "transport/http/friend_controller.h"

#include "protocol/dto/friend/friend_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/drogon.h>

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    case protocol::error::ErrorCode::kFriendAlreadyExists:
    case protocol::error::ErrorCode::kFriendRequestAlreadyPending:
    case protocol::error::ErrorCode::kFriendRequestAlreadyHandled:
        return drogon::k409Conflict;
    case protocol::error::ErrorCode::kOk:
        return drogon::k200OK;
    case protocol::error::ErrorCode::kInternalError:
    default:
        return drogon::k500InternalServerError;
    }
}

}  // namespace

void FriendController::listFriends(
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

    friendService_.listFriends(
        *accessToken,
        [sharedCallback, requestId](
            std::vector<protocol::dto::friendship::FriendListItemView> items) mutable {
            Json::Value data(Json::objectValue);
            data["friends"] = protocol::dto::friendship::toJson(items);
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

void FriendController::sendFriendRequest(
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

    protocol::dto::friendship::SendFriendRequest sendRequest;
    std::string parseError;
    if (!protocol::dto::friendship::parseSendFriendRequest(
            *json, sendRequest, parseError))
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError));
        return;
    }

    friendService_.sendFriendRequest(
        std::move(sendRequest),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::friendship::FriendRequestItemView item) mutable {
            Json::Value data(Json::objectValue);
            data["request"] = protocol::dto::friendship::toJson(item);
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

void FriendController::listIncomingFriendRequests(
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

    friendService_.listIncomingFriendRequests(
        *accessToken,
        [sharedCallback, requestId](
            std::vector<protocol::dto::friendship::FriendRequestItemView> items) mutable {
            Json::Value data(Json::objectValue);
            data["requests"] = protocol::dto::friendship::toJson(items);
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

void FriendController::listOutgoingFriendRequests(
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

    friendService_.listOutgoingFriendRequests(
        *accessToken,
        [sharedCallback, requestId](
            std::vector<protocol::dto::friendship::FriendRequestItemView> items) mutable {
            Json::Value data(Json::objectValue);
            data["requests"] = protocol::dto::friendship::toJson(items);
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

void FriendController::acceptFriendRequest(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string requestId) const
{
    const std::string resolvedRequestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            resolvedRequestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    friendService_.acceptFriendRequest(
        std::move(requestId),
        *accessToken,
        [sharedCallback, resolvedRequestId](
            protocol::dto::friendship::FriendRequestItemView item) mutable {
            Json::Value data(Json::objectValue);
            data["request"] = protocol::dto::friendship::toJson(item);
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                resolvedRequestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, resolvedRequestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           resolvedRequestId,
                                           error.code,
                                           error.message));
        });
}

void FriendController::rejectFriendRequest(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string requestId) const
{
    const std::string resolvedRequestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k401Unauthorized,
            resolvedRequestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    friendService_.rejectFriendRequest(
        std::move(requestId),
        *accessToken,
        [sharedCallback, resolvedRequestId](
            protocol::dto::friendship::FriendRequestItemView item) mutable {
            Json::Value data(Json::objectValue);
            data["request"] = protocol::dto::friendship::toJson(item);
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                resolvedRequestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, resolvedRequestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           resolvedRequestId,
                                           error.code,
                                           error.message));
        });
}

}  // namespace chatserver::transport::http
