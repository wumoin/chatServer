#pragma once

#include <string>

namespace chatserver::infra::id {

// IdGenerator 当前只负责生成认证模块需要的业务主键。
// 第一版先基于 UUID 派生稳定字符串，满足：
// 1) 不依赖数据库自增；
// 2) 可以直接作为对外 ID 返回；
// 3) 长度受控，能匹配现有表结构。
class IdGenerator {
  public:
    /**
     * @brief 生成新的用户业务主键。
     * @return 格式为 `u_` + 24 位十六进制字符的用户 ID，例如 `u_abcd1234...`。
     */
    std::string nextUserId() const;

    /**
     * @brief 生成新的设备会话业务主键。
     * @return 格式为 `ds_` + 24 位十六进制字符的设备会话 ID。
     */
    std::string nextDeviceSessionId() const;

    /**
     * @brief 生成新的好友申请业务主键。
     * @return 格式为 `fr_` + 24 位十六进制字符的好友申请 ID。
     */
    std::string nextFriendRequestId() const;

    /**
     * @brief 生成新的会话业务主键。
     * @return 格式为 `c_` + 24 位十六进制字符的会话 ID。
     */
    std::string nextConversationId() const;

    /**
     * @brief 生成新的消息业务主键。
     * @return 格式为 `m_` + 24 位十六进制字符的消息 ID。
     */
    std::string nextMessageId() const;

  private:
    /**
     * @brief 生成带业务前缀的字符串 ID。
     * @param prefix 业务前缀，例如 `u_`。
     * @param payloadLength 前缀之后保留的十六进制字符长度。
     * @return 拼接后的业务 ID 字符串。
     */
    std::string buildPrefixedId(const std::string &prefix,
                                std::size_t payloadLength) const;
};

}  // namespace chatserver::infra::id
