#pragma once

namespace chatserver::protocol::error {

// 当前对外 HTTP 接口统一使用整数业务码。
// 约定：
// - 0 表示成功；
// - 4xxxx 表示客户端请求问题；
// - 5xxxx 表示服务端内部问题。
enum class ErrorCode : int {
    // 成功。
    kOk = 0,
    // 请求体不是合法 JSON。
    kInvalidJson = 40000,
    // 参数类型错误或业务校验失败。
    kInvalidArgument = 40001,
    // 资源不存在。
    kNotFound = 40400,
    // 访问令牌缺失、损坏或格式非法。
    kInvalidAccessToken = 40102,
    // 账号或密码不正确。
    kInvalidCredentials = 40101,
    // 账号唯一约束冲突。
    kAccountAlreadyExists = 40901,
    // 当前设备已有活跃登录会话。
    kDeviceAlreadyLoggedIn = 40902,
    // 目标用户已经是当前用户的好友。
    kFriendAlreadyExists = 40903,
    // 当前用户和目标用户之间已有待处理的好友申请。
    kFriendRequestAlreadyPending = 40904,
    // 好友申请已经被处理，不能再次同意或拒绝。
    kFriendRequestAlreadyHandled = 40905,
    // 账号已被禁用。
    kAccountDisabled = 40301,
    // 账号已被锁定。
    kAccountLocked = 40302,
    // 当前用户无权操作目标资源。
    kForbidden = 40300,
    // 未归类的服务端内部错误。
    kInternalError = 50000,
};

/**
 * @brief 返回指定业务错误码的默认说明文本。
 * @param code 业务错误码。
 * @return 与错误码对应的默认英文消息字符串。
 */
inline const char *defaultMessage(const ErrorCode code)
{
    // 给 controller 提供一份最小默认消息表。
    // 如果某个具体分支需要更细的说明，仍然可以由 service 覆盖。
    switch (code)
    {
    case ErrorCode::kOk:
        return "ok";
    case ErrorCode::kInvalidJson:
        return "invalid json body";
    case ErrorCode::kInvalidArgument:
        return "invalid argument";
    case ErrorCode::kNotFound:
        return "resource not found";
    case ErrorCode::kInvalidAccessToken:
        return "invalid access token";
    case ErrorCode::kInvalidCredentials:
        return "invalid credentials";
    case ErrorCode::kAccountAlreadyExists:
        return "account already exists";
    case ErrorCode::kDeviceAlreadyLoggedIn:
        return "device already logged in";
    case ErrorCode::kFriendAlreadyExists:
        return "friend already exists";
    case ErrorCode::kFriendRequestAlreadyPending:
        return "friend request already pending";
    case ErrorCode::kFriendRequestAlreadyHandled:
        return "friend request already handled";
    case ErrorCode::kAccountDisabled:
        return "account disabled";
    case ErrorCode::kAccountLocked:
        return "account locked";
    case ErrorCode::kForbidden:
        return "forbidden";
    case ErrorCode::kInternalError:
    default:
        return "internal server error";
    }
}

}  // namespace chatserver::protocol::error
