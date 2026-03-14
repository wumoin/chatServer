#pragma once

#include "service/auth_service.h"

#include <drogon/HttpController.h>

namespace chatserver::transport::http {

// AuthController 是当前第一个正式业务 HttpController。
// 它只负责：
// 1) 接收 HTTP 请求；
// 2) 解析请求 JSON；
// 3) 调用 AuthService；
// 4) 把结果封装成统一响应。
//
// 它不直接操作数据库，也不直接处理密码哈希。
class AuthController : public drogon::HttpController<AuthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::registerUser, "/api/v1/auth/register", drogon::Post);
    ADD_METHOD_TO(AuthController::loginUser, "/api/v1/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::logoutUser, "/api/v1/auth/logout", drogon::Post);
    METHOD_LIST_END

    /**
     * @brief 处理用户注册 HTTP 请求。
     * @param request Drogon 封装后的 HTTP 请求对象。
     * @param callback 生成响应后回写给客户端的回调函数。
     */
    void registerUser(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 处理用户登录 HTTP 请求。
     * @param request Drogon 封装后的 HTTP 请求对象。
     * @param callback 生成响应后回写给客户端的回调函数。
     */
    void loginUser(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 处理当前设备登出 HTTP 请求。
     * @param request Drogon 封装后的 HTTP 请求对象。
     * @param callback 生成响应后回写给客户端的回调函数。
     */
    void logoutUser(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

  private:
    service::AuthService authService_;
};

}  // namespace chatserver::transport::http
