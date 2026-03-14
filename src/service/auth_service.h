#pragma once

#include "protocol/dto/auth/login_dto.h"
#include "protocol/dto/auth/register_dto.h"
#include "protocol/error/error_code.h"
#include "repository/device_session_repository.h"
#include "repository/user_repository.h"

#include <functional>
#include <optional>
#include <string>

namespace chatserver::service {

// ServiceError 是 service 层对 controller 暴露的统一失败结果。
// 它的职责很单一：
// 1) 带一个稳定的业务错误码；
// 2) 带一条可以直接回给客户端的消息。
struct ServiceError {
    // 业务错误码，由 protocol/error/error_code.h 定义。
    protocol::error::ErrorCode code;
    // 面向 controller / 客户端的文本说明。
    std::string message;
};

// LoginRequestContext 表示登录请求里那些不直接来自 JSON body，
// 但需要一起参与设备会话落库和风控审计的元信息。
struct LoginRequestContext {
    std::string loginIp;
    std::optional<std::string> userAgent;
};

class AuthService {
  public:
    using RegisterSuccess =
        std::function<void(protocol::dto::auth::RegisterUserView)>;
    using RegisterFailure = std::function<void(ServiceError)>;
    using LoginSuccess =
        std::function<void(protocol::dto::auth::LoginResultView)>;
    using LoginFailure = std::function<void(ServiceError)>;

    /**
     * @brief 执行注册用例。
     * @param request 注册请求 DTO。
     * @param onSuccess 注册成功后的回调，参数为可直接回给接口层的用户视图。
     * @param onFailure 注册失败后的回调，参数为统一的业务错误结果。
     */
    void registerUser(protocol::dto::auth::RegisterRequest request,
                      RegisterSuccess &&onSuccess,
                      RegisterFailure &&onFailure) const;

    /**
     * @brief 执行登录用例，并创建新的设备级登录会话。
     * @param request 登录请求 DTO。
     * @param context 由 HTTP 层提取的请求上下文，例如客户端 IP 和 User-Agent。
     * @param onSuccess 登录成功后的回调，参数为登录成功返回视图。
     * @param onFailure 登录失败后的回调，参数为统一业务错误。
     */
    void loginUser(protocol::dto::auth::LoginRequest request,
                   LoginRequestContext context,
                   LoginSuccess &&onSuccess,
                   LoginFailure &&onFailure) const;

  private:
    /**
     * @brief 校验并规范化注册请求中的业务字段。
     * @param request 待校验的注册请求，校验成功时其中部分字段可能被规范化，例如 nickname 被 trim。
     * @param error 校验失败时写入的业务错误结果。
     * @return true 表示业务规则校验通过；false 表示校验失败。
     */
    bool validateRegisterRequest(
        protocol::dto::auth::RegisterRequest &request,
        ServiceError &error) const;

    /**
     * @brief 校验并规范化登录请求中的业务字段和设备字段。
     * @param request 待校验的登录请求。
     * @param error 校验失败时写入的业务错误结果。
     * @return true 表示校验通过；false 表示存在非法输入。
     */
    bool validateLoginRequest(protocol::dto::auth::LoginRequest &request,
                              ServiceError &error) const;

    // 当前认证服务依赖的最小 repository。
    // 第一版暂时直接以成员对象持有，后续如果需要依赖注入，再抽到 app/application 层统一装配。
    repository::UserRepository userRepository_;
    repository::DeviceSessionRepository deviceSessionRepository_;
};

}  // namespace chatserver::service
