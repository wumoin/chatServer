#include "infra/security/password_hasher.h"

#include <crypt.h>
#include <drogon/utils/Utilities.h>

#include <array>
#include <stdexcept>
#include <string>

namespace chatserver::infra::security {
namespace {

// bcrypt cost 越高，抗暴力破解能力越强，但单次哈希耗时也越长。
// 当前先用 12，作为本地开发和最小服务端落地的折中值。
constexpr unsigned long kBcryptCost = 12;

}  // namespace

std::string PasswordHasher::hashPassword(
    const std::string_view plainPassword) const
{
    if (plainPassword.empty())
    {
        throw std::invalid_argument("Password must not be empty");
    }

    const std::string setting = makeBcryptSetting();

    crypt_data data {};
    data.initialized = 0;

    // crypt_r() 是线程安全版本，适合放在服务端并发请求里使用。
    char *hashed =
        crypt_r(std::string(plainPassword).c_str(), setting.c_str(), &data);
    if (hashed == nullptr)
    {
        throw std::runtime_error("crypt_r() failed while hashing password");
    }

    return hashed;
}

bool PasswordHasher::verifyPassword(const std::string_view plainPassword,
                                    const std::string_view hashedPassword) const
{
    if (plainPassword.empty() || hashedPassword.empty())
    {
        return false;
    }

    crypt_data data {};
    data.initialized = 0;

    // bcrypt 校验不需要单独解析 salt。
    // 直接把“已有哈希串”作为 crypt_r() 的 setting 传回去，libcrypt 会自动按其中的算法和盐重算。
    char *computed = crypt_r(std::string(plainPassword).c_str(),
                             std::string(hashedPassword).c_str(),
                             &data);
    if (computed == nullptr)
    {
        return false;
    }

    return std::string_view(computed) == hashedPassword;
}

std::string PasswordHasher::makeBcryptSetting() const
{
    // libcrypt 会根据 setting 中的算法前缀和 cost 自动生成 bcrypt salt。
    // 这里喂入一段随机字符串作为盐生成输入，避免不同密码生成相同 setting。
    const std::string randomInput = drogon::utils::genRandomString(32);

    std::array<char, CRYPT_GENSALT_OUTPUT_SIZE> output {};
    char *setting = crypt_gensalt_rn("$2b$",
                                     kBcryptCost,
                                     randomInput.data(),
                                     randomInput.size(),
                                     output.data(),
                                     output.size());
    if (setting == nullptr)
    {
        throw std::runtime_error(
            "crypt_gensalt_rn() failed while creating bcrypt setting");
    }

    return setting;
}

}  // namespace chatserver::infra::security
