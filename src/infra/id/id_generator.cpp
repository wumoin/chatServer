#include "infra/id/id_generator.h"

#include <drogon/utils/Utilities.h>

#include <cctype>
#include <string>

namespace chatserver::infra::id {

std::string IdGenerator::nextUserId() const
{
    // users.user_id 当前统一走 `u_` 前缀。
    // 后续如果要生成 conversation_id / message_id，可以继续复用同一套路由。
    return buildPrefixedId("u_", 24);
}

std::string IdGenerator::buildPrefixedId(const std::string &prefix,
                                         const std::size_t payloadLength) const
{
    // Drogon 的 UUID 默认包含连字符，例如：
    // 550e8400-e29b-41d4-a716-446655440000
    //
    // 这里把连字符去掉后再截取定长片段，原因是：
    // 1) 现有 users.user_id 字段只有 VARCHAR(32)；
    // 2) 我们还需要给 ID 加业务前缀；
    // 3) 认证阶段先用短一些的稳定 ID 就足够。
    const std::string uuid = drogon::utils::getUuid(true);

    std::string compact;
    compact.reserve(uuid.size());
    for (const char ch : uuid)
    {
        if (std::isxdigit(static_cast<unsigned char>(ch)) != 0)
        {
            compact.push_back(ch);
        }
    }

    if (compact.size() > payloadLength)
    {
        compact.resize(payloadLength);
    }

    // 当前不额外做数据库查重。
    // UUID 截断后理论上有极低概率碰撞，但对第一版注册闭环已足够；
    // 后续如果要进一步提高稳健性，可以补重试或切到更长 payload。
    return prefix + compact;
}

}  // namespace chatserver::infra::id
