#pragma once

#include <string>
#include <string_view>

namespace chatserver::infra::security {

// PasswordHasher 当前只负责认证域最小需要的密码哈希能力。
// 第一版统一使用 bcrypt，原因是：
// 1) SQL 约束已经允许 bcrypt；
// 2) 本机可直接复用 libcrypt；
// 3) 登录实现时可以继续复用同一套 verify 逻辑。
class PasswordHasher {
  public:
    /**
     * @brief 生成明文密码对应的 bcrypt 哈希。
     * @param plainPassword 待哈希的明文密码。
     * @return 可直接存入数据库的完整 bcrypt 哈希串，内部已包含算法标识、cost 和 salt。
     */
    std::string hashPassword(std::string_view plainPassword) const;

    /**
     * @brief 校验明文密码是否匹配已有 bcrypt 哈希。
     * @param plainPassword 待校验的明文密码。
     * @param hashedPassword 数据库中已保存的 bcrypt 哈希串。
     * @return true 表示密码匹配；false 表示不匹配或校验失败。
     */
    bool verifyPassword(std::string_view plainPassword,
                        std::string_view hashedPassword) const;

  private:
    /**
     * @brief 生成 bcrypt 所需的 setting / salt 字符串。
     * @return 形如 `$2b$12$...` 的 bcrypt setting，内部包含算法前缀、cost 和随机盐。
     */
    std::string makeBcryptSetting() const;
};

}  // namespace chatserver::infra::security
