#include "transport/http/auth_controller.h"

#include "protocol/dto/auth/login_dto.h"
#include "protocol/dto/auth/register_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/drogon.h>

#include <memory>
#include <string>
#include <utility>

namespace chatserver::transport::http {
namespace {

std::string resolveRequestId(const drogon::HttpRequestPtr &request)
{
    // 优先回显客户端传来的 X-Request-Id，便于前后端联调排查。
    // 如果客户端没传，就由服务端补一个 UUID。
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

// 统一构造认证接口响应。
// 当前注册接口和后续登录 / refresh 都会沿用同一种顶层结构：
// {
//   "code": ...,
//   "message": "...",
//   "request_id": "...",
//   "data": { ... }
// }
//
// 这样 controller 层不会到处散落手写 JSON 结构，后续要补公共字段也更容易统一调整。
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

}  // namespace

void AuthController::registerUser(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    // 先固定 request_id，再进入任何业务逻辑。
    // 这样无论后面走成功、参数错误还是数据库失败，响应里都会带同一个追踪号。
    const std::string requestId = resolveRequestId(request);

    // Drogon 的 callback 是 move-only 语义；而注册接口后续可能走多个异步分支。
    // 因此这里先收口成 shared callback，避免在成功 / 失败 lambda 中重复 move 后悬空。
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    // 第 1 层校验：请求体必须能被 Drogon 解析成 JSON。
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

    // 第 2 层校验：字段存在性和 JSON 类型正确性。
    // 更细的业务规则继续交给 AuthService。
    protocol::dto::auth::RegisterRequest registerRequest;
    std::string parseError;
    if (!protocol::dto::auth::parseRegisterRequest(*json,
                                                   registerRequest,
                                                   parseError))
    {
        (*sharedCallback)(makeResponse(drogon::k400BadRequest,
                                       requestId,
                                       protocol::error::ErrorCode::kInvalidArgument,
                                       parseError));
        return;
    }

    // 进入注册业务流程：
    // - controller 不直接写库；
    // - 只等待 AuthService 的成功 / 失败结果，再映射成 HTTP 响应。
    authService_.registerUser(
        std::move(registerRequest),
        [sharedCallback, requestId](
            protocol::dto::auth::RegisterUserView user) mutable {
            Json::Value data(Json::objectValue);
            data["user"] = protocol::dto::auth::toJson(user);

            // 注册成功固定返回 201 Created。
            (*sharedCallback)(makeResponse(
                drogon::k201Created,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                std::move(data)));
        },
        [sharedCallback, requestId](
            service::ServiceError error) mutable {
            // 业务错误码和 HTTP 状态码不是一回事。
            // 这里做的是“service error -> HTTP status” 的最后一跳映射。
            drogon::HttpStatusCode statusCode = drogon::k500InternalServerError;
            if (error.code == protocol::error::ErrorCode::kInvalidArgument)
            {
                statusCode = drogon::k400BadRequest;
            }
            else if (error.code ==
                     protocol::error::ErrorCode::kAccountAlreadyExists)
            {
                statusCode = drogon::k409Conflict;
            }

            (*sharedCallback)(makeResponse(statusCode,
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void AuthController::loginUser(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    const std::string requestId = resolveRequestId(request);

    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

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

    protocol::dto::auth::LoginRequest loginRequest;
    std::string parseError;
    if (!protocol::dto::auth::parseLoginRequest(*json, loginRequest, parseError))
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError));
        return;
    }

    service::LoginRequestContext context;
    context.loginIp = request->getPeerAddr().toIp();
    const auto userAgent = request->getHeader("User-Agent");
    if (!userAgent.empty())
    {
        context.userAgent = userAgent;
    }

    authService_.loginUser(
        std::move(loginRequest),
        std::move(context),
        [sharedCallback, requestId](
            protocol::dto::auth::LoginResultView result) mutable {
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                protocol::dto::auth::toJson(result)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            drogon::HttpStatusCode statusCode = drogon::k500InternalServerError;
            if (error.code == protocol::error::ErrorCode::kInvalidArgument)
            {
                statusCode = drogon::k400BadRequest;
            }
            else if (error.code ==
                     protocol::error::ErrorCode::kInvalidCredentials)
            {
                statusCode = drogon::k401Unauthorized;
            }
            else if (error.code ==
                         protocol::error::ErrorCode::kAccountDisabled ||
                     error.code == protocol::error::ErrorCode::kAccountLocked)
            {
                statusCode = drogon::k403Forbidden;
            }

            (*sharedCallback)(makeResponse(statusCode,
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

}  // namespace chatserver::transport::http
