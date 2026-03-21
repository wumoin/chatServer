#include "infra/security/token_provider.h"

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include <array>
#include <chrono>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace chatserver::infra::security {
namespace {

/**
 * @brief token 模块配置。
 */
struct TokenSettings
{
    std::string accessTokenSecret = "change_me_in_app_json";
    std::int64_t accessTokenExpiresInSec = 7200;
    std::int64_t refreshTokenExpiresInSec = 2592000;
};

/**
 * @brief token 模块运行时状态。
 */
struct TokenState
{
    bool initialized = false;
    TokenSettings settings;
    std::mutex mutex;
};

TokenState g_tokenState;

std::int64_t nowEpochSec()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

bool hasObjectMember(const Json::Value &object, const char *key)
{
    return object.isObject() && object.isMember(key) && !object[key].isNull();
}

void readOptionalInt64(const Json::Value &object,
                       const char *key,
                       std::int64_t &out,
                       const char *fieldName)
{
    if (!hasObjectMember(object, key))
    {
        return;
    }

    if (!object[key].isInt64() && !object[key].isUInt64() &&
        !object[key].isInt())
    {
        throw std::runtime_error(std::string(fieldName) +
                                 " must be an integer");
    }

    out = object[key].asInt64();
}

void readOptionalString(const Json::Value &object,
                        const char *key,
                        std::string &out,
                        const char *fieldName)
{
    if (!hasObjectMember(object, key))
    {
        return;
    }

    if (!object[key].isString())
    {
        throw std::runtime_error(std::string(fieldName) + " must be a string");
    }

    out = object[key].asString();
}

TokenSettings loadSettings(const std::string &configPath)
{
    // token_provider 当前不依赖 Drogon 配置对象，直接从 app.json 读取
    // chatserver.auth 这一小段配置，保证它在 infra 层可独立初始化。
    std::ifstream input(configPath);
    if (!input.is_open())
    {
        throw std::runtime_error("failed to open token config file: " +
                                 configPath);
    }

    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["allowComments"] = true;

    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors))
    {
        throw std::runtime_error("failed to parse token config: " + errors);
    }

    TokenSettings settings;

    const Json::Value &chatServerObject = root["chatserver"];
    const Json::Value &authObject = chatServerObject["auth"];
    if (authObject.isObject())
    {
        readOptionalString(authObject,
                           "access_token_secret",
                           settings.accessTokenSecret,
                           "chatserver.auth.access_token_secret");
        readOptionalInt64(authObject,
                          "access_token_expires_in_sec",
                          settings.accessTokenExpiresInSec,
                          "chatserver.auth.access_token_expires_in_sec");
        readOptionalInt64(authObject,
                          "refresh_token_expires_in_sec",
                          settings.refreshTokenExpiresInSec,
                          "chatserver.auth.refresh_token_expires_in_sec");
    }

    if (settings.accessTokenSecret.empty())
    {
        throw std::runtime_error(
            "chatserver.auth.access_token_secret must not be empty");
    }

    if (settings.accessTokenExpiresInSec <= 0)
    {
        throw std::runtime_error(
            "chatserver.auth.access_token_expires_in_sec must be positive");
    }

    if (settings.refreshTokenExpiresInSec <= 0)
    {
        throw std::runtime_error(
            "chatserver.auth.refresh_token_expires_in_sec must be positive");
    }

    return settings;
}

std::string signPayload(const std::string &payload)
{
    // 当前 access token 不是标准 JWT，而是项目内的简化格式：
    // prefix.base64(payload).sha256(secret.payload)
    const std::string raw = g_tokenState.settings.accessTokenSecret + "." + payload;
    return drogon::utils::getSha256(raw);
}

std::string encodePayload(const Json::Value &payload)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return drogon::utils::base64EncodeUnpadded(
        Json::writeString(builder, payload),
        true);
}

bool decodePayload(const std::string &encodedPayload, Json::Value &payload)
{
    std::string decoded;
    try
    {
        decoded = drogon::utils::base64Decode(encodedPayload);
    }
    catch (const std::exception &)
    {
        return false;
    }

    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(decoded.data(),
                         decoded.data() + decoded.size(),
                         &payload,
                         &errors);
}

}  // namespace

void TokenProvider::initialize(const std::string &configPath)
{
    std::lock_guard<std::mutex> lock(g_tokenState.mutex);
    if (g_tokenState.initialized)
    {
        return;
    }

    g_tokenState.settings = loadSettings(configPath);
    g_tokenState.initialized = true;
}

bool TokenProvider::isInitialized()
{
    return g_tokenState.initialized;
}

std::string TokenProvider::issueAccessToken(const std::string &userId,
                                            const std::string &deviceSessionId) const
{
    // access token 自包含最小 claims，便于 HTTP 和 WS 都能只靠 token 恢复
    // user_id / device_session_id / 过期时间。
    Json::Value payload(Json::objectValue);
    payload["user_id"] = userId;
    payload["device_session_id"] = deviceSessionId;
    payload["iat"] = Json::Int64(nowEpochSec());
    payload["exp"] = Json::Int64(nowEpochSec() +
                                 g_tokenState.settings.accessTokenExpiresInSec);

    const std::string encodedPayload = encodePayload(payload);
    return "access." + encodedPayload + "." + signPayload(encodedPayload);
}

bool TokenProvider::verifyAccessToken(const std::string &token,
                                      AccessTokenClaims *claims) const
{
    // 这里先验证“结构合法 + 签名正确 + claims 类型正确”。
    // 是否已经过期留给调用方结合 claims.expiresAtSec 再判断，
    // 这样不同入口可以决定自己的过期错误语义。
    const auto firstDot = token.find('.');
    const auto secondDot =
        firstDot == std::string::npos ? std::string::npos : token.find('.', firstDot + 1);
    if (firstDot == std::string::npos || secondDot == std::string::npos)
    {
        return false;
    }

    if (token.substr(0, firstDot) != "access")
    {
        return false;
    }

    const std::string encodedPayload =
        token.substr(firstDot + 1, secondDot - firstDot - 1);
    const std::string signature = token.substr(secondDot + 1);
    if (signature != signPayload(encodedPayload))
    {
        return false;
    }

    Json::Value payload;
    if (!decodePayload(encodedPayload, payload) || !payload.isObject())
    {
        return false;
    }

    if (!payload["user_id"].isString() ||
        !payload["device_session_id"].isString() ||
        !payload["iat"].isInt64() ||
        !payload["exp"].isInt64())
    {
        return false;
    }

    if (claims != nullptr)
    {
        claims->userId = payload["user_id"].asString();
        claims->deviceSessionId = payload["device_session_id"].asString();
        claims->issuedAtSec = payload["iat"].asInt64();
        claims->expiresAtSec = payload["exp"].asInt64();
    }

    return true;
}

std::string TokenProvider::issueRefreshToken() const
{
    // refresh token 走完全不透明的随机串方案，不自包含任何 claims。
    std::array<unsigned char, 32> bytes {};
    drogon::utils::secureRandomBytes(bytes.data(), bytes.size());
    return "refresh." +
           drogon::utils::base64EncodeUnpadded(bytes.data(), bytes.size(), true);
}

std::string TokenProvider::hashOpaqueToken(const std::string &token) const
{
    // 数据库侧只保存 refresh token 哈希值，避免原文落库。
    return drogon::utils::getSha256(
        g_tokenState.settings.accessTokenSecret + "." + token);
}

std::int64_t TokenProvider::accessTokenExpiresInSec() const
{
    return g_tokenState.settings.accessTokenExpiresInSec;
}

std::int64_t TokenProvider::refreshTokenExpiresInSec() const
{
    return g_tokenState.settings.refreshTokenExpiresInSec;
}

}  // namespace chatserver::infra::security
