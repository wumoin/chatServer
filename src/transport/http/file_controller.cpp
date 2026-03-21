#include "transport/http/file_controller.h"

#include "protocol/dto/file/file_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/MultiPart.h>
#include <drogon/drogon.h>

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// FileController 是聊天附件域的 HTTP 入口：
// - uploadFile 负责认证、multipart 解析和统一响应；
// - downloadFile 负责把 attachment_id 对应的文件直接流给客户端。
//
// 它不决定附件是否属于某个会话消息，也不直接操作数据库。
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
    case protocol::error::ErrorCode::kOk:
        return drogon::k200OK;
    case protocol::error::ErrorCode::kInternalError:
    default:
        return drogon::k500InternalServerError;
    }
}

std::string resolveMimeType(const drogon::HttpFile &file)
{
    // multipart 如果带了已知内容类型就尽量沿用；识别不出来时统一回退为 octet-stream。
    switch (file.getContentType())
    {
    case drogon::CT_APPLICATION_JSON:
        return "application/json";
    case drogon::CT_TEXT_PLAIN:
        return "text/plain";
    case drogon::CT_APPLICATION_PDF:
        return "application/pdf";
    case drogon::CT_APPLICATION_ZIP:
        return "application/zip";
    case drogon::CT_IMAGE_APNG:
        return "image/apng";
    case drogon::CT_IMAGE_BMP:
        return "image/bmp";
    case drogon::CT_IMAGE_GIF:
        return "image/gif";
    case drogon::CT_IMAGE_JPG:
        return "image/jpeg";
    case drogon::CT_IMAGE_PNG:
        return "image/png";
    case drogon::CT_IMAGE_SVG_XML:
        return "image/svg+xml";
    case drogon::CT_IMAGE_TIFF:
        return "image/tiff";
    case drogon::CT_IMAGE_WEBP:
        return "image/webp";
    default:
        return "application/octet-stream";
    }
}

std::string resolveMediaKind(const drogon::HttpFile &file)
{
    // 目前附件域只粗分成 image / file 两类，
    // 更细的预览策略由客户端根据 mimeType 再细分。
    return file.getFileType() == drogon::FT_IMAGE ? "image" : "file";
}

const drogon::HttpFile *resolveUploadFile(const drogon::MultiPartParser &parser)
{
    // 约定优先取字段名为 file 的上传文件，便于客户端和文档统一；
    // 如果调用方没按名字传，就回退到第一个文件字段，尽量放宽兼容性。
    const auto &files = parser.getFiles();
    for (const auto &file : files)
    {
        if (file.getItemName() == "file")
        {
            return &file;
        }
    }

    if (files.empty())
    {
        return nullptr;
    }

    return &files.front();
}

}  // namespace

void FileController::uploadFile(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    // controller 这一层只做三件事：
    // 1. 解析认证与 multipart
    // 2. 调用 FileService
    // 3. 把结果包装成统一 JSON 响应
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

    const auto *file = resolveUploadFile(parser);
    if (file == nullptr)
    {
        (*sharedCallback)(makeResponse(
            drogon::k400BadRequest,
            requestId,
            protocol::error::ErrorCode::kInvalidArgument,
            "file is required"));
        return;
    }

    service::UploadAttachmentRequest uploadRequest;
    uploadRequest.originalFileName = file->getFileName();
    uploadRequest.mimeType = resolveMimeType(*file);
    uploadRequest.mediaKind = resolveMediaKind(*file);
    // controller 负责把 multipart 文件对象压成统一内存请求，
    // 后续落盘、生成 storage key、返回附件视图都由 FileService 处理。
    uploadRequest.content.assign(file->fileData(), file->fileLength());

    fileService_.uploadAttachment(
        std::move(uploadRequest),
        *accessToken,
        [sharedCallback, requestId](
            protocol::dto::file::AttachmentView attachment) mutable {
            Json::Value data(Json::objectValue);
            data["attachment"] = protocol::dto::file::toJson(attachment);
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

void FileController::downloadFile(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string attachmentId) const
{
    // 下载成功时直接返回文件流，而不是再包一层 JSON；只有失败才回统一错误响应。
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

    fileService_.getAttachmentFile(
        std::move(attachmentId),
        *accessToken,
        [request, sharedCallback](
            service::DownloadAttachmentResult result) mutable {
            // 下载成功直接透传底层文件，避免再包 JSON 影响浏览器 / 客户端下载体验。
            (*sharedCallback)(drogon::HttpResponse::newFileResponse(
                result.absolutePath,
                "",
                drogon::CT_CUSTOM,
                result.mimeType.empty() ? "application/octet-stream"
                                        : result.mimeType,
                request));
        },
        [sharedCallback, requestId](service::ServiceError error) mutable {
            (*sharedCallback)(makeResponse(mapServiceErrorToStatus(error),
                                           requestId,
                                           error.code,
                                           error.message));
        });
}

}  // namespace chatserver::transport::http
