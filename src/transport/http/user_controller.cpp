#include "transport/http/user_controller.h"

#include "protocol/dto/user/profile_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/MultiPart.h>
#include <drogon/drogon.h>

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// UserController 是用户资料和头像相关 HTTP 接口入口。
// 当前涵盖：
// - 临时头像上传
// - 资料更新
// - 用户搜索
// - 按 user_id 获取头像文件
//
// multipart 解析、query/path 读取在这里做，具体业务仍下放到 UserService。
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

void UserController::searchUserByAccount(
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

    const auto account = request->getOptionalParameter<std::string>("account");
    if (!account.has_value())
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            "account is required"));
        return;
    }

    // 搜索用户是只读查询：
    // 它只返回“该账号是否存在以及基础资料”，不在这里做任何好友或会话副作用。
    userService_.searchUserByAccount(
        *account,
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::user::UserSearchResultView result) mutable {
            (*sharedCallback)(makeResponse(
                drogon::k200OK,
                requestId,
                protocol::error::ErrorCode::kOk,
                protocol::error::defaultMessage(
                    protocol::error::ErrorCode::kOk),
                protocol::dto::user::toJson(result)));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void UserController::uploadTemporaryAvatar(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    const std::string requestId = resolveRequestId(request);

    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0)
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            "failed to parse multipart body"));
        return;
    }

    const auto &files = parser.getFiles();
    if (files.empty())
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            "avatar file is required"));
        return;
    }

    const auto &file = files.front();
    if (file.getFileType() != drogon::FT_IMAGE)
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            "avatar file must be an image"));
        return;
    }

    service::TemporaryAvatarUploadRequest uploadRequest;
    uploadRequest.originalFileName = file.getFileName();
    // 当前临时头像上传只关心“这是不是图片”和原始字节内容，
    // 后续真正落为用户头像时再由 UserService 决定正式 storage key。
    uploadRequest.contentType = "image/upload";
    uploadRequest.content.assign(file.fileData(), file.fileLength());

    userService_.uploadTemporaryAvatar(
        std::move(uploadRequest),
        [sharedCallback, requestId](
            service::TemporaryAvatarUploadView result) mutable {
            Json::Value data(Json::objectValue);
            data["avatar_upload_key"] = result.avatarUploadKey;
            data["preview_url"] = result.previewUrl;

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

void UserController::previewTemporaryAvatar(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    const std::string requestId = resolveRequestId(request);
    const auto avatarUploadKey = request->getOptionalParameter<std::string>(
        "avatar_upload_key");
    if (!avatarUploadKey.has_value())
    {
        callback(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            "avatar_upload_key is required"));
        return;
    }

    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    // 预览接口成功时直接回文件流，便于注册页立刻展示；
    // 失败才回统一 JSON 错误响应。
    userService_.getTemporaryAvatarFile(
        *avatarUploadKey,
        [request, sharedCallback](
            service::FileResolveResult result) mutable {
            (*sharedCallback)(drogon::HttpResponse::newFileResponse(
                result.absolutePath, "", drogon::CT_NONE, "", request));
        },
        [sharedCallback, requestId](
            service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

void UserController::updateProfile(
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

    protocol::dto::user::UpdateUserProfileRequest updateRequest;
    std::string parseError;
    if (!protocol::dto::user::parseUpdateUserProfileRequest(*json,
                                                            updateRequest,
                                                            parseError))
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            parseError));
        return;
    }

    userService_.updateProfile(
        std::move(updateRequest),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::user::UserProfileView user) mutable {
            Json::Value data(Json::objectValue);
            data["user"] = protocol::dto::user::toJson(user);
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

void UserController::getUserAvatar(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string userId) const
{
    const std::string requestId = resolveRequestId(request);
    auto sharedCallback =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    // 正式头像下载接口当前按 user_id 公开读取，不额外要求 access token。
    // controller 成功时直接返回文件流，交由客户端自行解码。
    userService_.getUserAvatarFile(
        std::move(userId),
        [request, sharedCallback](
            service::FileResolveResult result) mutable {
            (*sharedCallback)(drogon::HttpResponse::newFileResponse(
                result.absolutePath, "", drogon::CT_NONE, "", request));
        },
        [sharedCallback, requestId](
            service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

}  // namespace chatserver::transport::http
