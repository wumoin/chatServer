#pragma once

#include "service/file_service.h"

#include <drogon/HttpController.h>
#include <drogon/RequestStream.h>

namespace chatserver::transport::http {

class FileController : public drogon::HttpController<FileController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FileController::uploadFile,
                  "/api/v1/files/upload",
                  drogon::Post);
    ADD_METHOD_TO(FileController::downloadFile,
                  "/api/v1/files/{1}",
                  drogon::Get);
    METHOD_LIST_END

    /**
     * @brief 上传一个临时聊天附件文件。
     * @param request HTTP 请求对象。
     * @param streamCtx 当前请求的流式读取上下文。
     * @param callback HTTP 响应回调。
     */
    void uploadFile(
        const drogon::HttpRequestPtr &request,
        drogon::RequestStreamPtr &&streamCtx,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 下载指定附件文件。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param attachmentId 路由中的附件 ID。
     */
    void downloadFile(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string attachmentId) const;

  private:
    service::FileService fileService_;
};

}  // namespace chatserver::transport::http
