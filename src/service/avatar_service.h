#pragma once

#include "service/service_error.h"

#include <functional>
#include <optional>
#include <string>

namespace chatserver::service {

/**
 * @brief 临时头像上传请求。
 */
struct TemporaryAvatarUploadRequest
{
    std::string originalFileName;
    std::string contentType;
    std::string content;
};

/**
 * @brief 临时头像上传成功后返回给接口层的视图。
 */
struct TemporaryAvatarUploadView
{
    std::string avatarUploadKey;
    std::string previewUrl;
};

/**
 * @brief 头像服务。
 *
 * 当前职责：
 * 1) 接收未登录用户或已登录用户上传的临时头像；
 * 2) 把临时头像 `avatar_upload_key` 确认成正式头像存储 key；
 * 3) 提供临时头像和正式头像的文件定位与清理能力。
 */
class AvatarService
{
  public:
    /**
     * @brief 上传一个临时头像。
     * @param request 临时头像上传请求。
     * @param out 上传成功后的返回视图。
     * @param error 上传失败时写入的业务错误。
     * @return true 表示上传成功；false 表示上传失败。
     */
    bool uploadTemporaryAvatar(const TemporaryAvatarUploadRequest &request,
                               TemporaryAvatarUploadView &out,
                               ServiceError &error) const;

    /**
     * @brief 把临时头像上传 key 转成正式头像存储 key。
     * @param avatarUploadKey 临时头像上传 key。
     * @param avatarStorageKey 成功时返回正式头像 storage key。
     * @param error 失败时写入业务错误。
     * @return true 表示确认成功；false 表示确认失败。
     */
    bool confirmAvatarUploadKey(const std::string &avatarUploadKey,
                                std::string &avatarStorageKey,
                                ServiceError &error) const;

    /**
     * @brief 解析临时头像上传 key 对应的文件绝对路径。
     * @param avatarUploadKey 临时头像上传 key。
     * @param absolutePath 成功时写入的文件绝对路径。
     * @param error 失败时写入业务错误。
     * @return true 表示解析成功；false 表示解析失败。
     */
    bool resolveTemporaryAvatarPath(const std::string &avatarUploadKey,
                                    std::string &absolutePath,
                                    ServiceError &error) const;

    /**
     * @brief 解析正式头像存储 key 对应的文件绝对路径。
     * @param avatarStorageKey 正式头像 storage key。
     * @param absolutePath 成功时写入的文件绝对路径。
     * @param error 失败时写入业务错误。
     * @return true 表示解析成功；false 表示解析失败。
     */
    bool resolveStoredAvatarPath(const std::string &avatarStorageKey,
                                 std::string &absolutePath,
                                 ServiceError &error) const;

    /**
     * @brief 清理某个 storage key 对应的文件。
     * @param storageKey 待删除的存储 key。
     */
    void removeStorageKeyQuietly(const std::string &storageKey) const;

    /**
     * @brief 清理一个可选的 storage key。
     * @param storageKey 待删除的可选存储 key。
     */
    void removeStorageKeyQuietly(
        const std::optional<std::string> &storageKey) const;

  private:
    /**
     * @brief 判断上传 key 是否来自临时目录。
     * @param avatarUploadKey 临时头像上传 key。
     * @return true 表示路径位于 `tmp/` 目录下；false 表示不是临时头像 key。
     */
    bool isTemporaryAvatarUploadKey(const std::string &avatarUploadKey) const;
};

}  // namespace chatserver::service
