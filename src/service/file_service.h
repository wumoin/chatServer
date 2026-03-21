#pragma once

#include "protocol/dto/file/file_dto.h"
#include "repository/attachment_repository.h"
#include "service/service_error.h"

#include <functional>
#include <cstdint>
#include <optional>
#include <string>

namespace chatserver::service {

// 聊天附件上传大小上限统一提升到 1 GB。
// 这个常量会同时被：
// 1) FileController 的流式上传阶段；
// 2) FileService 的业务校验阶段；
// 共同复用，避免框架层、controller 层和 service 层各自维护一套不同阈值。
inline constexpr std::uint64_t kTemporaryAttachmentMaxBytes =
    1024ULL * 1024ULL * 1024ULL;

/**
 * @brief 临时附件上传请求。
 *
 * 这个请求只负责把文件先放进临时目录，不直接写 `attachments` 表。
 * 真正的正式附件确认会发生在后续的消息发送业务里。
 */
struct TemporaryAttachmentUploadRequest
{
    // 原始文件名，用于后续 storage key 后缀、响应展示和正式附件记录。
    std::string originalFileName;
    // multipart part 上报的 MIME 类型。
    std::string mimeType;
    // 业务媒体类别，当前只允许 image / file。
    std::string mediaKind;
    // 小文件路径仍可直接走内存内容导入。
    std::string content;
    // 大文件流式上传时，controller 会先把内容写到 staging file，
    // 然后把 staging file 的本地路径传给 service。
    std::string stagedFilePath;
    // 当前上传对象的字节大小：
    // - 走 content 路径时通常等于 content.size()；
    // - 走 stagedFilePath 路径时由 controller 在流式写盘时累加得到。
    std::uint64_t sizeBytes{0};
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

/**
 * @brief 临时附件上传成功后返回给接口层的视图。
 */
struct TemporaryAttachmentUploadView
{
    std::string attachmentUploadKey;
    std::string fileName;
    std::string mimeType;
    std::int64_t sizeBytes{0};
    std::string mediaKind;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

/**
 * @brief 把临时上传准备成正式附件后的结果。
 *
 * 这里的 `attachment` 已经对应正式文件和 `attachments` 记录，
 * 但临时上传文件仍会暂时保留，直到调用方在消息落库成功后显式清理。
 */
struct PreparedAttachmentResult
{
    std::string attachmentUploadKey;
    repository::AttachmentRecord attachment;
};

struct DownloadAttachmentResult
{
    std::string absolutePath;
    std::string mimeType;
};

class FileService
{
  public:
    using UploadTemporaryAttachmentSuccess =
        std::function<void(TemporaryAttachmentUploadView)>;
    using PrepareAttachmentSuccess =
        std::function<void(PreparedAttachmentResult)>;
    using DownloadAttachmentSuccess =
        std::function<void(DownloadAttachmentResult)>;
    using Failure = std::function<void(ServiceError)>;

    /**
     * @brief 上传一个临时聊天附件文件。
     * @param request 临时上传请求。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 上传成功回调。
     * @param onFailure 上传失败回调。
     */
    void uploadTemporaryAttachment(
        TemporaryAttachmentUploadRequest request,
        std::string accessToken,
        UploadTemporaryAttachmentSuccess &&onSuccess,
        Failure &&onFailure) const;

    /**
     * @brief 把一次临时附件上传准备成正式附件。
     * @param attachmentUploadKey 临时上传返回的 upload key。
     * @param uploaderUserId 当前准备该附件的用户 ID。
     * @param expectedMediaKind 调用方期望的媒体类别；为空表示不额外限制。
     * @param onSuccess 准备成功回调。
     * @param onFailure 准备失败回调。
     */
    void prepareAttachmentForMessage(
        std::string attachmentUploadKey,
        std::string uploaderUserId,
        std::optional<std::string> expectedMediaKind,
        PrepareAttachmentSuccess &&onSuccess,
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

    /**
     * @brief 安静地删除一次临时附件上传留下的文件和元数据。
     * @param attachmentUploadKey 临时上传 key。
     */
    void removeTemporaryUploadQuietly(
        const std::string &attachmentUploadKey) const;

    /**
     * @brief 安静地回滚一个已经准备成功的正式附件。
     * @param attachment 已创建的正式附件记录。
     */
    void rollbackPreparedAttachmentQuietly(
        const repository::AttachmentRecord &attachment) const;

  private:
    /**
     * @brief 校验并规范化临时上传请求。
     * @param request 待校验请求。
     * @param error 校验失败时写入业务错误。
     * @return true 表示校验通过；false 表示失败。
     */
    bool validateTemporaryAttachmentUploadRequest(
        TemporaryAttachmentUploadRequest &request,
        ServiceError &error) const;

    /**
     * @brief 安静地删除某个存储 key。
     * @param storageKey 待删除的附件存储 key。
     */
    void removeStorageKeyQuietly(const std::string &storageKey) const;

    repository::AttachmentRepository attachmentRepository_;
};

}  // namespace chatserver::service
