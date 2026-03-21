#pragma once

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace chatserver::repository {

struct CreateAttachmentParams
{
    std::string attachmentId;
    std::string uploaderUserId;
    std::string storageProvider;
    std::string storageKey;
    std::string originalFileName;
    std::string mimeType;
    std::int64_t sizeBytes{0};
    std::optional<std::string> sha256;
    std::string mediaKind;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

struct AttachmentRecord
{
    std::string attachmentId;
    std::string uploaderUserId;
    std::string storageProvider;
    std::string storageKey;
    std::string originalFileName;
    std::string mimeType;
    std::int64_t sizeBytes{0};
    std::optional<std::string> sha256;
    std::string mediaKind;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
    std::int64_t createdAtMs{0};
};

class AttachmentRepository
{
  public:
    using CreateAttachmentSuccess = std::function<void(AttachmentRecord)>;
    using FindAttachmentByIdSuccess =
        std::function<void(std::optional<AttachmentRecord>)>;
    using DeleteAttachmentSuccess = std::function<void(bool)>;
    using RepositoryFailure = std::function<void(std::string)>;

    /**
     * @brief 新增一条附件元数据记录。
     * @param params 待落库的附件字段。
     * @param onSuccess 写入成功回调。
     * @param onFailure 写入失败回调。
     */
    void createAttachment(CreateAttachmentParams params,
                          CreateAttachmentSuccess &&onSuccess,
                          RepositoryFailure &&onFailure) const;

    /**
     * @brief 按附件 ID 查询附件元数据。
     * @param attachmentId 附件 ID。
     * @param onSuccess 查询成功回调；不存在时返回空值。
     * @param onFailure 查询失败回调。
     */
    void findAttachmentById(std::string attachmentId,
                            FindAttachmentByIdSuccess &&onSuccess,
                            RepositoryFailure &&onFailure) const;

    /**
     * @brief 按附件 ID 删除一条附件元数据记录。
     * @param attachmentId 待删除的附件 ID。
     * @param onSuccess 删除完成回调；参数表示是否真的删到了记录。
     * @param onFailure 删除失败回调。
     */
    void deleteAttachmentById(std::string attachmentId,
                              DeleteAttachmentSuccess &&onSuccess,
                              RepositoryFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return 当前服务统一使用的 Drogon 数据库客户端指针。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository
