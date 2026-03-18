#pragma once

#include "protocol/error/error_code.h"

#include <string>

namespace chatserver::service {

/**
 * @brief service 层对 controller 暴露的统一失败结果。
 *
 * 它的职责很单一：
 * 1) 带一个稳定的业务错误码；
 * 2) 带一条可以直接回给客户端的消息。
 */
struct ServiceError
{
    /**
     * @brief 业务错误码。
     */
    protocol::error::ErrorCode code;

    /**
     * @brief 面向 controller / 客户端的文本说明。
     */
    std::string message;
};

}  // namespace chatserver::service
