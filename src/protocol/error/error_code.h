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
    // 账号唯一约束冲突。
    kAccountAlreadyExists = 40901,
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
    case ErrorCode::kAccountAlreadyExists:
        return "account already exists";
    case ErrorCode::kInternalError:
    default:
        return "internal server error";
    }
}

}  // namespace chatserver::protocol::error
