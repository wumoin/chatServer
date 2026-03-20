#pragma once

#include "protocol/dto/file/file_dto.h"
#include "repository/attachment_repository.h"
#include "service/service_error.h"

#include <functional>
#include <optional>
#include <string>

namespace chatserver::service {

struct UploadAttachmentRequest
{
    std::string originalFileName;
    std::string mimeType;
    std::string mediaKind;
    std::string content;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

struct DownloadAttachmentResult
{
    std::string absolutePath;
    std::string mimeType;
};

class FileService
{
  public:
    using UploadAttachmentSuccess =
        std::function<void(protocol::dto::file::AttachmentView)>;
    using DownloadAttachmentSuccess =
        std::function<void(DownloadAttachmentResult)>;
    using Failure = std::function<void(ServiceError)>;

    /**
     * @brief 上传一个聊天附件文件。
     * @param request 上传请求。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 上传成功回调。
     * @param onFailure 上传失败回调。
     */
    void uploadAttachment(UploadAttachmentRequest request,
                          std::string accessToken,
                          UploadAttachmentSuccess &&onSuccess,
                          Failure &&onFailure) const;

    /**
     * @brief 获取指定附件文件的绝对路径和 MIME 类型。
     * @param attachmentId 附件 ID。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 解析成功回调。
     * @param onFailure 解析失败回调。
     */
    void getAttachmentFile(std::string attachmentId,
                           std::string accessToken,
                           DownloadAttachmentSuccess &&onSuccess,
                           Failure &&onFailure) const;

  private:
    /**
     * @brief 校验并规范化上传请求。
     * @param request 待校验请求。
     * @param error 校验失败时写入业务错误。
     * @return true 表示校验通过；false 表示失败。
     */
    bool validateUploadAttachmentRequest(UploadAttachmentRequest &request,
                                         ServiceError &error) const;

    /**
     * @brief 把仓储记录转成接口视图。
     * @param record 附件记录。
     * @return 对外附件视图。
     */
    protocol::dto::file::AttachmentView toAttachmentView(
        const repository::AttachmentRecord &record) const;

    /**
     * @brief 安静地删除某个存储 key。
     * @param storageKey 待删除的附件存储 key。
     */
    void removeStorageKeyQuietly(const std::string &storageKey) const;

    repository::AttachmentRepository attachmentRepository_;
};

}  // namespace chatserver::service
