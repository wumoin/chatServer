#include "transport/http/file_controller.h"

#include "protocol/dto/file/file_dto.h"
#include "protocol/error/error_code.h"

#include <drogon/RequestStream.h>
#include <drogon/drogon.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
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

std::string resolveMimeType(const drogon::MultipartHeader &header)
{
    // 流式 multipart 场景下，我们直接使用 part header 自带的 contentType。
    // 若调用方没带这一项，则退回为 octet-stream，后续业务层再按 media_kind 校验。
    const std::string mimeType = trimCopy(header.contentType);
    return mimeType.empty() ? "application/octet-stream" : mimeType;
}

std::string resolveMediaKind(const std::string &mimeType)
{
    // 当前附件域仍只粗分成 image / file 两类。
    // 与之前基于 HttpFile::getFileType() 的逻辑相比，这里改成直接根据
    // multipart header 中的 MIME 类型做判断，更适合流式读取场景。
    return mimeType.rfind("image/", 0) == 0 ? "image" : "file";
}

std::optional<int> parseOptionalPositiveIntParameter(const std::string &value)
{
    const std::string trimmedValue = trimCopy(value);
    if (trimmedValue.empty())
    {
        return std::nullopt;
    }

    try
    {
        const long long parsed = std::stoll(trimmedValue);
        if (parsed <= 0 ||
            parsed > static_cast<long long>(std::numeric_limits<int>::max()))
        {
            return std::nullopt;
        }
        return static_cast<int>(parsed);
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

std::filesystem::path buildStreamUploadStagePath(const std::string &requestId,
                                                 const std::string &fileName)
{
    std::filesystem::path stageDir =
        std::filesystem::temp_directory_path() / "chatserver-stream-uploads";
    std::error_code errorCode;
    std::filesystem::create_directories(stageDir, errorCode);
    if (errorCode)
    {
        throw std::runtime_error("failed to create stream upload stage dir: " +
                                 stageDir.string() + ", error: " +
                                 errorCode.message());
    }

    const std::string extension =
        std::filesystem::path(fileName).extension().string();
    return stageDir /
           ("upload_" + requestId + "_" +
            drogon::utils::getUuid(false).substr(0, 12) + extension + ".part");
}

void removeStageFileQuietly(const std::filesystem::path &stagePath)
{
    if (stagePath.empty())
    {
        return;
    }

    std::error_code errorCode;
    std::filesystem::remove(stagePath, errorCode);
}

struct StreamUploadState
{
    std::string requestId;
    std::string accessToken;
    std::function<void(const drogon::HttpResponsePtr &)> callback;
    std::string currentPartName;
    bool currentPartIsFile = false;
    bool receivedFilePart = false;
    std::string originalFileName;
    std::string mimeType;
    std::string mediaKind;
    std::string imageWidthText;
    std::string imageHeightText;
    std::filesystem::path stagePath;
    std::ofstream stageFile;
    std::uint64_t bytesWritten = 0;
    bool callbackInvoked = false;
};

void respondOnce(const std::shared_ptr<StreamUploadState> &state,
                 const drogon::HttpResponsePtr &response)
{
    if (!state || state->callbackInvoked)
    {
        return;
    }

    state->callbackInvoked = true;
    state->callback(response);
}

}  // namespace

void FileController::uploadFile(
    const drogon::HttpRequestPtr &request,
    drogon::RequestStreamPtr &&streamCtx,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    // 当前上传入口已经改成真正的 request stream 模式：
    // 1. 先校验认证；
    // 2. 再由 Drogon 的 multipart stream reader 逐段解析 part；
    // 3. 文件 part 的字节会直接写入 staging file，而不是先拼成一整块内存；
    // 4. 流结束后再调用 FileService 完成 upload key / metadata 的正式整理。
    const std::string requestId = resolveRequestId(request);

    const auto accessToken = resolveBearerAccessToken(request);
    if (!accessToken.has_value())
    {
        callback(makeResponse(
            drogon::k401Unauthorized,
            requestId,
            protocol::error::ErrorCode::kInvalidAccessToken,
            protocol::error::defaultMessage(
                protocol::error::ErrorCode::kInvalidAccessToken)));
        return;
    }

    if (!streamCtx)
    {
        callback(makeResponse(
            drogon::k500InternalServerError,
            requestId,
            protocol::error::ErrorCode::kInternalError,
            "request stream is unavailable"));
        return;
    }

    auto state = std::make_shared<StreamUploadState>();
    state->requestId = requestId;
    state->accessToken = *accessToken;
    state->callback = std::move(callback);

    streamCtx->setStreamReader(drogon::RequestStreamReader::newMultipartReader(
        request,
        [state](drogon::MultipartHeader header) {
            if (state->callbackInvoked)
            {
                return;
            }

            state->currentPartName = trimCopy(header.name);
            state->currentPartIsFile = !header.filename.empty();
            if (!state->currentPartIsFile)
            {
                return;
            }

            if (state->receivedFilePart)
            {
                respondOnce(state,
                            makeResponse(drogon::k400BadRequest,
                                         state->requestId,
                                         protocol::error::ErrorCode::kInvalidArgument,
                                         "only one file part is allowed"));
                return;
            }

            state->receivedFilePart = true;
            state->originalFileName =
                std::filesystem::path(header.filename).filename().string();
            state->mimeType = resolveMimeType(header);
            state->mediaKind = resolveMediaKind(state->mimeType);
            state->stagePath =
                buildStreamUploadStagePath(state->requestId,
                                           state->originalFileName);
            state->stageFile.open(state->stagePath,
                                  std::ios::binary | std::ios::trunc);
            if (!state->stageFile.is_open())
            {
                respondOnce(state,
                            makeResponse(drogon::k500InternalServerError,
                                         state->requestId,
                                         protocol::error::ErrorCode::kInternalError,
                                         "failed to open upload stage file"));
            }
        },
        [state](const char *data, const size_t length) {
            if (state->callbackInvoked || length == 0)
            {
                return;
            }

            if (state->currentPartIsFile)
            {
                if (!state->stageFile.is_open())
                {
                    respondOnce(state,
                                makeResponse(
                                    drogon::k500InternalServerError,
                                    state->requestId,
                                    protocol::error::ErrorCode::kInternalError,
                                    "upload stage file is unavailable"));
                    return;
                }

                if (state->bytesWritten + length >
                    service::kTemporaryAttachmentMaxBytes)
                {
                    respondOnce(state,
                                makeResponse(
                                    drogon::k400BadRequest,
                                    state->requestId,
                                    protocol::error::ErrorCode::kInvalidArgument,
                                    "file size must not exceed 1 GB"));
                    return;
                }

                state->stageFile.write(data,
                                       static_cast<std::streamsize>(length));
                if (!state->stageFile.good())
                {
                    respondOnce(state,
                                makeResponse(
                                    drogon::k500InternalServerError,
                                    state->requestId,
                                    protocol::error::ErrorCode::kInternalError,
                                    "failed to write upload stage file"));
                    return;
                }

                state->bytesWritten += length;
                return;
            }

            if (state->currentPartName == "image_width")
            {
                state->imageWidthText.append(data,
                                             static_cast<std::string::size_type>(
                                                 length));
            }
            else if (state->currentPartName == "image_height")
            {
                state->imageHeightText.append(data,
                                              static_cast<std::string::size_type>(
                                                  length));
            }
        },
        [this, state](std::exception_ptr ex) {
            if (state->stageFile.is_open())
            {
                state->stageFile.close();
            }

            if (state->callbackInvoked)
            {
                removeStageFileQuietly(state->stagePath);
                return;
            }

            if (ex)
            {
                removeStageFileQuietly(state->stagePath);
                respondOnce(state,
                            makeResponse(drogon::k400BadRequest,
                                         state->requestId,
                                         protocol::error::ErrorCode::kInvalidArgument,
                                         "failed to parse multipart body"));
                return;
            }

            if (!state->receivedFilePart)
            {
                removeStageFileQuietly(state->stagePath);
                respondOnce(state,
                            makeResponse(drogon::k400BadRequest,
                                         state->requestId,
                                         protocol::error::ErrorCode::kInvalidArgument,
                                         "file is required"));
                return;
            }

            service::TemporaryAttachmentUploadRequest uploadRequest;
            uploadRequest.originalFileName = state->originalFileName;
            uploadRequest.mimeType = state->mimeType;
            uploadRequest.mediaKind = state->mediaKind;
            uploadRequest.stagedFilePath = state->stagePath.string();
            uploadRequest.sizeBytes = state->bytesWritten;
            uploadRequest.imageWidth =
                parseOptionalPositiveIntParameter(state->imageWidthText);
            uploadRequest.imageHeight =
                parseOptionalPositiveIntParameter(state->imageHeightText);

            fileService_.uploadTemporaryAttachment(
                std::move(uploadRequest),
                state->accessToken,
                [state](service::TemporaryAttachmentUploadView upload) mutable {
                    removeStageFileQuietly(state->stagePath);

                    Json::Value data(Json::objectValue);
                    data["upload"] = protocol::dto::file::toJson(
                        protocol::dto::file::TemporaryAttachmentUploadView{
                            upload.attachmentUploadKey,
                            upload.fileName,
                            upload.mimeType,
                            upload.sizeBytes,
                            upload.mediaKind,
                            upload.imageWidth,
                            upload.imageHeight,
                        });
                    respondOnce(state,
                                makeResponse(
                                    drogon::k201Created,
                                    state->requestId,
                                    protocol::error::ErrorCode::kOk,
                                    protocol::error::defaultMessage(
                                        protocol::error::ErrorCode::kOk),
                                    std::move(data)));
                },
                [state](service::ServiceError error) mutable {
                    removeStageFileQuietly(state->stagePath);
                    respondOnce(state,
                                makeResponse(mapServiceErrorToStatus(error),
                                             state->requestId,
                                             error.code,
                                             error.message));
                });
        }));
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
