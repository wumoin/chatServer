#pragma once

#include "protocol/dto/user/profile_dto.h"
#include "repository/user_repository.h"
#include "service/avatar_service.h"
#include "service/service_error.h"

#include <functional>
#include <string>

namespace chatserver::service {

/**
 * @brief 文件定位结果。
 */
struct FileResolveResult
{
    std::string absolutePath;
};

class UserService
{
  public:
    using UploadTemporaryAvatarSuccess =
        std::function<void(TemporaryAvatarUploadView)>;
    using UpdateUserProfileSuccess =
        std::function<void(protocol::dto::user::UserProfileView)>;
    using ResolveFileSuccess = std::function<void(FileResolveResult)>;
    using Failure = std::function<void(ServiceError)>;

    /**
     * @brief 上传临时头像。
     * @param request 临时头像上传请求。
     * @param onSuccess 上传成功回调。
     * @param onFailure 上传失败回调。
     */
    void uploadTemporaryAvatar(TemporaryAvatarUploadRequest request,
                               UploadTemporaryAvatarSuccess &&onSuccess,
                               Failure &&onFailure) const;

    /**
     * @brief 更新当前登录用户的资料。
     * @param accessToken 当前登录态 access token。
     * @param request 更新资料请求。
     * @param onSuccess 更新成功回调。
     * @param onFailure 更新失败回调。
     */
    void updateProfile(protocol::dto::user::UpdateUserProfileRequest request,
                       std::string accessToken,
                       UpdateUserProfileSuccess &&onSuccess,
                       Failure &&onFailure) const;

    /**
     * @brief 获取某个用户的正式头像文件。
     * @param userId 用户 ID。
     * @param onSuccess 定位成功回调。
     * @param onFailure 定位失败回调。
     */
    void getUserAvatarFile(std::string userId,
                           ResolveFileSuccess &&onSuccess,
                           Failure &&onFailure) const;

    /**
     * @brief 获取某个临时头像上传 key 对应的预览文件。
     * @param avatarUploadKey 临时头像上传 key。
     * @param onSuccess 定位成功回调。
     * @param onFailure 定位失败回调。
     */
    void getTemporaryAvatarFile(std::string avatarUploadKey,
                                ResolveFileSuccess &&onSuccess,
                                Failure &&onFailure) const;

  private:
    /**
     * @brief 校验并规范化更新资料请求。
     * @param request 待校验的更新请求。
     * @param error 校验失败时写入的业务错误。
     * @return true 表示校验成功；false 表示失败。
     */
    bool validateUpdateProfileRequest(
        protocol::dto::user::UpdateUserProfileRequest &request,
        ServiceError &error) const;

    repository::UserRepository userRepository_;
    AvatarService avatarService_;
};

}  // namespace chatserver::service
