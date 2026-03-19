#pragma once

#include "service/user_service.h"

#include <drogon/HttpController.h>

namespace chatserver::transport::http {

class UserController : public drogon::HttpController<UserController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::searchUserByAccount,
                  "/api/v1/users/search",
                  drogon::Get);
    ADD_METHOD_TO(UserController::uploadTemporaryAvatar,
                  "/api/v1/users/avatar/temp",
                  drogon::Post);
    ADD_METHOD_TO(UserController::previewTemporaryAvatar,
                  "/api/v1/users/avatar/temp",
                  drogon::Get);
    ADD_METHOD_TO(UserController::updateProfile,
                  "/api/v1/users/me/profile",
                  drogon::Patch);
    ADD_METHOD_TO(UserController::getUserAvatar,
                  "/api/v1/users/{1}/avatar",
                  drogon::Get);
    METHOD_LIST_END

    /**
     * @brief 按账号搜索用户是否存在。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void searchUserByAccount(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 上传临时头像。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void uploadTemporaryAvatar(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 预览临时头像文件。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void previewTemporaryAvatar(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 更新当前登录用户资料。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void updateProfile(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 获取指定用户 ID 的正式头像文件。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param userId 路由中的用户 ID。
     */
    void getUserAvatar(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string userId) const;

  private:
    service::UserService userService_;
};

}  // namespace chatserver::transport::http
